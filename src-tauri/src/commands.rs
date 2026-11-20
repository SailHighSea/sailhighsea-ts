//! Tauri commands: the thin layer between the React UI and the engine / store.

use crate::engine::{self, net, torrent::Detail, update, TorrentResult, SOURCES, SRC_ALL};
use crate::paths::DataLocation;
use crate::store::{clean_query, Settings, Store};
use serde::Serialize;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter, Manager, State};
use tauri_plugin_opener::OpenerExt;

#[derive(Clone, Serialize)]
pub struct SourceView {
    pub state: &'static str, // idle | running | ok | failed
    pub count: usize,
}

#[derive(Clone, Serialize)]
pub struct SearchUpdate {
    pub gen: u64,
    pub query: String,
    pub busy: bool,
    pub results: Vec<TorrentResult>,
    pub sources: Vec<SourceView>,
}

struct Run {
    gen: u64,
    query: String,
    results: Vec<TorrentResult>,
    sources: Vec<SourceView>,
}

impl Run {
    fn snapshot(&self) -> SearchUpdate {
        SearchUpdate {
            gen: self.gen,
            query: self.query.clone(),
            busy: self.sources.iter().any(|s| s.state == "running"),
            results: self.results.iter().cloned().map(TorrentResult::with_magnet).collect(),
            sources: self.sources.clone(),
        }
    }
}

#[derive(Clone, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct LaunchArgs {
    pub query: String,
    pub type_only: bool,
    pub expand: bool,
}

impl LaunchArgs {
    /// `app [words to search for]`, plus the dev flags `--type=text` (fill the box, don't search) and `--expand` (open the first result).
    pub fn parse(args: &[String]) -> LaunchArgs {
        let mut l = LaunchArgs::default();
        let mut words = Vec::new();
        for a in args.iter().skip(1) {
            match a.as_str() {
                "--type=text" => l.type_only = true,
                "--expand" => l.expand = true,
                "--portable" => {}
                s if s.starts_with("--") => {}
                s => words.push(s.to_string()),
            }
        }
        l.query = clean_query(&words.join(" "));
        l
    }
}

pub struct AppState {
    pub store: Mutex<Store>,
    pub client: reqwest::Client,
    pub location: DataLocation,
    pub launch: LaunchArgs,
    run: Arc<Mutex<Run>>,
}

impl AppState {
    pub fn new(store: Store, location: DataLocation, launch: LaunchArgs) -> Self {
        AppState {
            store: Mutex::new(store),
            client: net::client(),
            location,
            launch,
            run: Arc::new(Mutex::new(Run { gen: 0, query: String::new(), results: Vec::new(), sources: Vec::new() })),
        }
    }
}

type St<'a> = State<'a, AppState>;

fn lock<'a>(s: &'a St) -> std::sync::MutexGuard<'a, Store> {
    s.store.lock().unwrap_or_else(|e| e.into_inner())
}

// ---- app info ---------------------------------------------------------------------------------------------------------------

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AppInfo {
    pub version: String,
    pub platform: String,
    pub arch: String,
    pub portable: bool,
    pub portable_fallback: bool,
    pub data_dir: String,
    pub mobile: bool,
    pub launch: LaunchArgs,
}

#[tauri::command]
pub fn app_info(app: AppHandle, state: St) -> AppInfo {
    AppInfo {
        version: app.package_info().version.to_string(),
        platform: std::env::consts::OS.into(),
        arch: std::env::consts::ARCH.into(),
        portable: state.location.portable,
        portable_fallback: state.location.portable_fallback,
        data_dir: state.location.dir.display().to_string(),
        mobile: cfg!(any(target_os = "android", target_os = "ios")),
        launch: state.launch.clone(),
    }
}

#[derive(Serialize)]
pub struct SourceInfo {
    pub name: &'static str,
    pub on: bool,
}

#[tauri::command]
pub fn sources_list() -> Vec<SourceInfo> {
    SOURCES.iter().map(|s| SourceInfo { name: s.name, on: s.on }).collect()
}

// ---- settings / favorites / history ---------------------------------------------------------------------------------------------

#[tauri::command]
pub fn settings_get(state: St) -> Settings {
    lock(&state).settings.clone()
}

#[tauri::command]
pub fn settings_set(state: St, mut settings: Settings) -> Result<Settings, String> {
    settings.normalize();
    let mut st = lock(&state);
    st.settings = settings;
    st.save_settings()?;
    Ok(st.settings.clone())
}

#[tauri::command]
pub fn favorites_get(state: St) -> Vec<TorrentResult> {
    lock(&state).favorites.iter().cloned().map(TorrentResult::with_magnet).collect()
}

/// Adds the result when it is not a favorite yet, removes it when it is. Returns whether it is a favorite now.
#[tauri::command]
pub fn fav_toggle(state: St, result: TorrentResult) -> Result<bool, String> {
    let mut st = lock(&state);
    let now = if st.fav_remove(&result.hash) { false } else { st.fav_add(result) };
    st.save_favorites()?;
    Ok(now)
}

#[tauri::command]
pub fn history_get(state: St) -> Vec<String> {
    lock(&state).history.clone()
}

#[tauri::command]
pub fn history_add(state: St, query: String) -> Result<Vec<String>, String> {
    let mut st = lock(&state);
    st.history_add(&query);
    st.save_history()?;
    Ok(st.history.clone())
}

#[tauri::command]
pub fn history_remove(state: St, query: String) -> Result<Vec<String>, String> {
    let mut st = lock(&state);
    st.history_remove(&query);
    st.save_history()?;
    Ok(st.history.clone())
}

#[tauri::command]
pub fn history_clear(state: St) -> Result<(), String> {
    let mut st = lock(&state);
    st.history.clear();
    st.save_history()
}

// ---- search ---------------------------------------------------------------------------------------------------------------------

/// Starts a search over the sources in `mask`; an unfinished earlier search is abandoned (its late answers are dropped).
/// Progress arrives as `search:update` events. Returns the search's generation number.
#[tauri::command]
pub fn search(app: AppHandle, state: St, query: String, mask: u32) -> Result<u64, String> {
    let query = clean_query(&query);
    if query.is_empty() {
        return Err("Type something to search for".into());
    }
    let mask = mask & SRC_ALL;
    if mask == 0 {
        return Err("Turn on at least one source in Settings".into());
    }
    {
        let mut st = lock(&state);
        st.history_add(&query);
        let _ = st.save_history();
    }
    let gen = {
        let mut run = state.run.lock().unwrap_or_else(|e| e.into_inner());
        run.gen += 1;
        run.query = query.clone();
        run.results.clear();
        run.sources = (0..engine::SRC_COUNT).map(|i| SourceView { state: if mask >> i & 1 == 1 { "running" } else { "idle" }, count: 0 }).collect();
        let _ = app.emit("search:update", run.snapshot());
        run.gen
    };
    for src in 0..engine::SRC_COUNT {
        if mask >> src & 1 == 0 {
            continue;
        }
        let client = state.client.clone();
        let run = state.run.clone();
        let app = app.clone();
        let query = query.clone();
        tauri::async_runtime::spawn(async move {
            let res = net::search_source(&client, src, &query).await;
            let snap = {
                let mut r = run.lock().unwrap_or_else(|e| e.into_inner());
                if r.gen != gen {
                    return;
                }
                match res {
                    Ok(rows) => {
                        r.sources[src] = SourceView { state: "ok", count: rows.len() };
                        for row in rows {
                            engine::merge(&mut r.results, row);
                        }
                    }
                    Err(()) => r.sources[src] = SourceView { state: "failed", count: 0 },
                }
                r.snapshot()
            };
            let _ = app.emit("search:update", snap);
        });
    }
    Ok(gen)
}

#[tauri::command]
pub async fn suggest(state: St<'_>, query: String) -> Result<Vec<String>, String> {
    let q = clean_query(&query);
    if q.is_empty() {
        return Ok(Vec::new());
    }
    Ok(net::suggest(&state.client, &q).await.unwrap_or_default())
}

#[tauri::command]
pub async fn detail(state: St<'_>, hash: String, tid: i64) -> Result<Detail, String> {
    if engine::take_hash(&hash).is_none() {
        return Err("not an info hash".into());
    }
    Ok(net::load_detail(&state.client, &hash, tid).await)
}

// ---- save .torrent / open links ------------------------------------------------------------------------------------------------------

fn safe_file_name(name: &str, fallback: &str) -> String {
    let s: String = name.chars().map(|c| if c.is_control() || "<>:\"/\\|?*".contains(c) { '_' } else { c }).collect();
    let s = engine::trunc(s.trim().trim_matches('.').to_string(), 100);
    if s.is_empty() { fallback.to_string() } else { s }
}

fn unique_path(dir: &PathBuf, stem: &str) -> PathBuf {
    let mut p = dir.join(format!("{}.torrent", stem));
    let mut n = 1;
    while p.exists() && n < 1000 {
        p = dir.join(format!("{} ({}).torrent", stem, n));
        n += 1;
    }
    p
}

/// Desktop: fetches the .torrent from a public cache and writes it to the Downloads folder.
/// Android: scoped storage forbids writing there directly, so the cache link is handed to the browser, whose download
/// manager saves it to Downloads (and a torrent app can open it from there).
/// Returns a message for the status bar.
#[tauri::command]
pub async fn save_torrent(app: AppHandle, state: St<'_>, hash: String, name: String) -> Result<String, String> {
    if engine::take_hash(&hash).is_none() {
        return Err("not an info hash".into());
    }
    #[cfg(target_os = "android")]
    {
        let _ = (&state, &name);
        let url = format!("https://itorrents.org/torrent/{}.torrent", hash.to_ascii_uppercase());
        app.opener().open_url(url, None::<&str>).map_err(|e| e.to_string())?;
        return Ok("Opened the .torrent download in your browser".into());
    }
    #[cfg(not(target_os = "android"))]
    {
        let data = net::fetch_torrent(&state.client, &hash).await.ok_or("No .torrent file found for this one")?;
        let dir = app.path().download_dir().map_err(|e| e.to_string())?;
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        let path = unique_path(&dir, &safe_file_name(&name, &hash));
        std::fs::write(&path, data).map_err(|e| e.to_string())?;
        Ok(format!("Saved {}", path.display()))
    }
}

/// Opens a magnet link in the user's torrent app, or the project page in the browser. Nothing else is allowed.
#[tauri::command]
pub fn open_url(app: AppHandle, url: String) -> Result<(), String> {
    let ok = (url.starts_with("magnet:?xt=urn:btih:") && !url.contains(char::is_whitespace)) || url.starts_with("https://github.com/SailHighSea/");
    if !ok {
        return Err("This kind of link is not allowed".into());
    }
    app.opener().open_url(url, None::<&str>).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn open_data_folder(app: AppHandle, state: St) -> Result<(), String> {
    #[cfg(desktop)]
    {
        return app.opener().open_path(state.location.dir.display().to_string(), None::<&str>).map_err(|e| e.to_string());
    }
    #[cfg(not(desktop))]
    {
        let _ = (app, state);
        Err("Not available on this device".into())
    }
}

// ---- updates ------------------------------------------------------------------------------------------------------------------------

#[derive(Serialize)]
pub struct UpdateInfo {
    pub tag: String,
    pub url: String,
    pub newer: bool,
}

/// None when offline or there is no release yet.
#[tauri::command]
pub async fn check_update(app: AppHandle, state: St<'_>, include_dev: bool) -> Result<Option<UpdateInfo>, String> {
    let current = app.package_info().version.to_string();
    Ok(update::latest_release(&state.client, include_dev).await.map(|r| {
        let newer = if include_dev { update::version_newer(&r.tag, &current) } else { update::version_core_newer(&r.tag, &current) };
        UpdateInfo { tag: r.tag, url: r.url, newer }
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launch_args() {
        let a: Vec<String> = ["app", "--portable", "ubuntu", "24.04", "--expand"].iter().map(|s| s.to_string()).collect();
        let l = LaunchArgs::parse(&a);
        assert_eq!(l.query, "ubuntu 24.04");
        assert!(l.expand && !l.type_only);
        assert!(LaunchArgs::parse(&["app".into(), "--type=text".into()]).type_only);
    }

    #[test]
    fn file_names() {
        assert_eq!(safe_file_name("a/b:c*?.mkv", "h"), "a_b_c__.mkv");
        assert_eq!(safe_file_name("  ..  ", "fallback"), "fallback");
    }
}
