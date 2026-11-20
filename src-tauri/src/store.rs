//! Settings, favorites and search history, kept as small JSON files in the data folder.
//! Files from the old C app (`torrentsearch-*.txt|tsv` next to the program) are imported once.

use crate::engine::{self, trunc, TorrentResult, SRC_ALL, SRC_COUNT};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

pub const FAV_MAX: usize = 500;
pub const HISTORY_MAX: usize = 50;
pub const QUERY_MAX: usize = 159;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", default)]
pub struct Settings {
    /// auto | light | dark | flat98 | win98 | mac7 | platinum | cde | gnome2
    pub theme: String,
    pub source_mask: u32,
    /// How many sources the file knew about; sources added since start in their default state.
    pub source_count: usize,
    /// seeders | size | newest | name
    pub sort: String,
    pub check_updates: bool,
    pub update_dev: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            theme: "auto".into(),
            source_mask: engine::default_mask(),
            source_count: SRC_COUNT,
            sort: "seeders".into(),
            check_updates: false,
            update_dev: false,
        }
    }
}

const THEMES: [&str; 10] = ["auto", "light", "dark", "glass", "flat98", "win98", "mac7", "platinum", "cde", "gnome2"];
const SORTS: [&str; 4] = ["seeders", "size", "newest", "name"];

impl Settings {
    /// Fixes up values read from disk (unknown theme, new sources, out-of-range mask).
    pub fn normalize(&mut self) {
        if !THEMES.contains(&self.theme.as_str()) {
            self.theme = "auto".into();
        }
        if !SORTS.contains(&self.sort.as_str()) {
            self.sort = "seeders".into();
        }
        self.source_mask &= SRC_ALL;
        if self.source_count > 0 {
            for s in self.source_count..SRC_COUNT {
                if (engine::default_mask() >> s) & 1 == 1 {
                    self.source_mask |= 1 << s;
                }
            }
        }
        self.source_count = SRC_COUNT;
    }
}

pub fn clean_query(q: &str) -> String {
    let q: String = q.chars().map(|c| if matches!(c, '\t' | '\r' | '\n') { ' ' } else { c }).collect();
    trunc(q.trim().to_string(), QUERY_MAX)
}

pub struct Store {
    pub dir: PathBuf,
    pub settings: Settings,
    pub favorites: Vec<TorrentResult>,
    pub history: Vec<String>,
}

fn read_json<T: for<'de> Deserialize<'de>>(path: &Path) -> Option<T> {
    serde_json::from_slice(&fs::read(path).ok()?).ok()
}

/// Writes through a temp file so a crash never leaves a half-written file.
fn write_json<T: Serialize>(path: &Path, value: &T) -> Result<(), String> {
    let data = serde_json::to_vec_pretty(value).map_err(|e| e.to_string())?;
    let tmp = path.with_extension("json.tmp");
    fs::write(&tmp, data).map_err(|e| e.to_string())?;
    fs::rename(&tmp, path).map_err(|e| e.to_string())
}

impl Store {
    /// Loads (or creates) the store in `dir`. `legacy_dir` is where the old C app kept its text files, if anywhere.
    pub fn load(dir: PathBuf, legacy_dir: Option<&Path>) -> Store {
        let _ = fs::create_dir_all(&dir);
        let mut st = Store { dir: dir.clone(), settings: Settings::default(), favorites: Vec::new(), history: Vec::new() };
        match read_json::<Settings>(&dir.join("settings.json")) {
            Some(s) => st.settings = s,
            None => {
                if let Some(l) = legacy_dir {
                    if let Some(s) = legacy::settings(&l.join("torrentsearch-settings.txt")) {
                        st.settings = s;
                    }
                }
            }
        }
        st.settings.normalize();
        match read_json::<Vec<TorrentResult>>(&dir.join("favorites.json")) {
            Some(f) => st.favorites = f,
            None => {
                if let Some(l) = legacy_dir {
                    st.favorites = legacy::favorites(&l.join("torrentsearch-favorites.tsv"));
                }
            }
        }
        st.favorites.truncate(FAV_MAX);
        match read_json::<Vec<String>>(&dir.join("history.json")) {
            Some(h) => st.history = h,
            None => {
                if let Some(l) = legacy_dir {
                    st.history = legacy::history(&l.join("torrentsearch-history.txt"));
                }
            }
        }
        st.history.truncate(HISTORY_MAX);
        st
    }

    pub fn save_settings(&self) -> Result<(), String> {
        write_json(&self.dir.join("settings.json"), &self.settings)
    }

    pub fn save_favorites(&self) -> Result<(), String> {
        write_json(&self.dir.join("favorites.json"), &self.favorites)
    }

    pub fn save_history(&self) -> Result<(), String> {
        write_json(&self.dir.join("history.json"), &self.history)
    }

    pub fn fav_find(&self, hash: &str) -> Option<usize> {
        self.favorites.iter().position(|f| f.hash == hash)
    }

    /// Newest first. False when it is full or already there.
    pub fn fav_add(&mut self, mut r: TorrentResult) -> bool {
        if self.favorites.len() >= FAV_MAX || self.fav_find(&r.hash).is_some() {
            return false;
        }
        r.magnet.clear();
        self.favorites.insert(0, r);
        true
    }

    pub fn fav_remove(&mut self, hash: &str) -> bool {
        match self.fav_find(hash) {
            Some(i) => {
                self.favorites.remove(i);
                true
            }
            None => false,
        }
    }

    /// Moves a repeated query to the top; blank queries are ignored.
    pub fn history_add(&mut self, query: &str) {
        let q = clean_query(query);
        if q.is_empty() {
            return;
        }
        self.history.retain(|h| *h != q);
        self.history.insert(0, q);
        self.history.truncate(HISTORY_MAX);
    }

    pub fn history_remove(&mut self, query: &str) {
        self.history.retain(|h| h != query);
    }
}

/// Readers for the files the C app wrote.
pub mod legacy {
    use super::*;

    pub fn settings(path: &Path) -> Option<Settings> {
        let text = fs::read_to_string(path).ok()?;
        let mut s = Settings { source_count: 0, ..Default::default() };
        for line in text.lines() {
            let Some((k, v)) = line.split_once('=') else { continue };
            match k.trim() {
                "theme" => s.theme = v.trim().to_string(),
                "sources" => {
                    s.source_mask = v.trim().parse::<u64>().unwrap_or(0) as u32;
                    if s.source_count == 0 {
                        s.source_count = 28; // files from before sourceCount had 28 sources
                    }
                }
                "sourceCount" => s.source_count = v.trim().parse().unwrap_or(0),
                "sort" => s.sort = SORTS.get(v.trim().parse::<usize>().unwrap_or(0)).unwrap_or(&"seeders").to_string(),
                "checkUpdates" => s.check_updates = v.trim() == "1",
                "updateDev" => s.update_dev = v.trim() == "1",
                _ => {}
            }
        }
        if s.source_count == 0 {
            s.source_mask = engine::default_mask();
            s.source_count = SRC_COUNT;
        }
        Some(s)
    }

    /// hash, source, seeders, leechers, size, added, detail, name [, tid, files] separated by tabs
    pub fn favorites(path: &Path) -> Vec<TorrentResult> {
        let Ok(text) = fs::read_to_string(path) else { return Vec::new() };
        let mut out = Vec::new();
        for line in text.lines() {
            let f: Vec<&str> = line.split('\t').collect();
            if f.len() < 8 || f[0].len() != 40 || out.len() >= FAV_MAX {
                continue;
            }
            let src: usize = f[1].parse().unwrap_or(0);
            out.push(TorrentResult {
                hash: f[0].to_string(),
                source: if src < SRC_COUNT { src } else { 0 },
                seeders: f[2].parse().unwrap_or(0),
                leechers: f[3].parse().unwrap_or(0),
                size: f[4].parse().unwrap_or(0),
                added: f[5].parse().unwrap_or(0),
                detail: f[6].to_string(),
                name: f[7].to_string(),
                tid: f.get(8).and_then(|v| v.parse().ok()).unwrap_or(0),
                files: f.get(9).and_then(|v| v.parse().ok()).unwrap_or(0),
                ..Default::default()
            });
        }
        out
    }

    pub fn history(path: &Path) -> Vec<String> {
        let Ok(text) = fs::read_to_string(path) else { return Vec::new() };
        text.lines().map(str::trim).filter(|l| !l.is_empty()).take(HISTORY_MAX).map(String::from).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("shs-test-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(&d).unwrap();
        d
    }

    fn res(hash: &str, name: &str) -> TorrentResult {
        TorrentResult { hash: hash.into(), name: name.into(), ..Default::default() }
    }

    #[test]
    fn favorites_roundtrip() {
        let dir = temp("fav");
        let mut st = Store::load(dir.clone(), None);
        let mut a = res("abcdef0123456789abcdef0123456789abcdef01", "First");
        a.source = engine::SRC_NYAA;
        a.seeders = 5;
        a.size = 1234;
        a.added = 99;
        let b = res("1111111111111111111111111111111111111111", "Second");
        assert!(st.fav_add(a.clone()));
        assert!(!st.fav_add(a.clone())); // duplicate
        assert!(st.fav_add(b.clone()));
        assert_eq!(st.favorites.len(), 2);
        assert_eq!(st.favorites[0].name, "Second"); // newest first
        st.save_favorites().unwrap();
        let g = Store::load(dir.clone(), None);
        assert_eq!(g.favorites.len(), 2);
        assert_eq!(g.fav_find(&a.hash), Some(1));
        assert_eq!((g.favorites[1].source, g.favorites[1].seeders, g.favorites[1].size, g.favorites[1].added), (engine::SRC_NYAA, 5, 1234, 99));
        let mut g = g;
        assert!(g.fav_remove(&b.hash));
        assert_eq!(g.favorites.len(), 1);
        assert!(g.fav_find(&b.hash).is_none());
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn history_rules() {
        let dir = temp("hist");
        let mut st = Store::load(dir.clone(), None);
        st.history_add("ubuntu");
        st.history_add("debian");
        st.history_add("ubuntu"); // moves to the top
        st.history_add("   "); // blank: ignored
        assert_eq!(st.history, vec!["ubuntu", "debian"]);
        st.save_history().unwrap();
        assert_eq!(Store::load(dir.clone(), None).history, vec!["ubuntu", "debian"]);
        for i in 0..80 {
            st.history_add(&format!("q{}", i));
        }
        assert_eq!(st.history.len(), HISTORY_MAX);
        assert_eq!(st.history[0], "q79");
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn settings_normalize_and_legacy() {
        let mut s = Settings { theme: "bogus".into(), sort: "x".into(), source_mask: u32::MAX, source_count: 28, ..Default::default() };
        s.normalize();
        assert_eq!((s.theme.as_str(), s.sort.as_str()), ("auto", "seeders"));
        assert_eq!(s.source_mask & !SRC_ALL, 0);
        assert_eq!(s.source_count, SRC_COUNT);

        let dir = temp("legacy");
        fs::write(dir.join("torrentsearch-settings.txt"), "theme=dark\nsources=5\nsort=2\ncheckUpdates=1\n").unwrap();
        fs::write(
            dir.join("torrentsearch-favorites.tsv"),
            "abcdef0123456789abcdef0123456789abcdef01\t2\t5\t1\t1234\t99\tdet\tFirst name\t7\t3\nshort\t0\n",
        )
        .unwrap();
        fs::write(dir.join("torrentsearch-history.txt"), "ubuntu\n\ndebian\n").unwrap();
        let st = Store::load(dir.join("data"), Some(&dir));
        assert_eq!((st.settings.theme.as_str(), st.settings.sort.as_str(), st.settings.check_updates), ("dark", "newest", true));
        assert_eq!(st.settings.source_mask & 0b111, 0b101); // sources 0 and 2 from the file
        assert_ne!(st.settings.source_mask >> 28, 0); // sources added after the file was written come in at their default
        assert_eq!(st.favorites.len(), 1);
        assert_eq!((st.favorites[0].tid, st.favorites[0].files, st.favorites[0].name.as_str()), (7, 3, "First name"));
        assert_eq!(st.history, vec!["ubuntu", "debian"]);
        let _ = fs::remove_dir_all(dir);
    }
}
