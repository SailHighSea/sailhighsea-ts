pub mod commands;
pub mod engine;
pub mod paths;
pub mod store;

use commands::{AppState, LaunchArgs};
use tauri::{Manager, WebviewUrl, WebviewWindowBuilder};

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_clipboard_manager::init())
        .setup(|app| {
            let args: Vec<String> = std::env::args().collect();
            let app_data = app.path().app_data_dir().unwrap_or_else(|_| std::env::temp_dir().join("sailhighsea-torrentsearch"));
            let loc = paths::resolve(&args, app_data);
            let store = store::Store::load(loc.dir.clone(), loc.legacy_dir.as_deref());
            #[cfg(desktop)]
            let (portable, data_dir) = (loc.portable, loc.dir.clone());

            // state first, so the page can call commands the moment it loads
            app.manage(AppState::new(store, loc, LaunchArgs::parse(&args)));

            #[allow(unused_mut)]
            let mut win = WebviewWindowBuilder::new(app, "main", WebviewUrl::App("index.html".into())).title("SailHighSea TorrentSearch");
            #[cfg(desktop)]
            {
                win = win.inner_size(1040.0, 720.0).min_inner_size(420.0, 520.0).decorations(false);
                if portable {
                    // keep the WebView's own cache and storage inside the portable folder too
                    win = win.data_directory(data_dir.join("webview"));
                }
            }
            win.build()?;
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_info,
            commands::sources_list,
            commands::settings_get,
            commands::settings_set,
            commands::favorites_get,
            commands::fav_toggle,
            commands::history_get,
            commands::history_add,
            commands::history_remove,
            commands::history_clear,
            commands::search,
            commands::suggest,
            commands::detail,
            commands::save_torrent,
            commands::open_url,
            commands::open_data_folder,
            commands::check_update,
        ])
        .run(tauri::generate_context!())
        .expect("error while running SailHighSea TorrentSearch");
}
