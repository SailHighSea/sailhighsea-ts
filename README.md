# SailHighSea TorrentSearch

A small, portable torrent search tool for Windows, macOS, Linux and Android, built with Tauri 2 (Rust backend, React front end). It searches public torrent indexes, merges the answers by info-hash and hands the magnet link to your own torrent app. It never downloads or shares files itself.

> Only download material you have the right to. Search results come from third-party sites.

It has the same look as SailHighSea SimpleSync: custom title bar, icon rail, nine themes (System, Light, Dark, Flat 98, Win98, Mac System 7, Mac Platinum, Linux CDE, Linux GNOME 2).

## Features

- **32 sources** switched on or off in Settings. The Pirate Bay, YTS, Nyaa (and Sukebei, off by default), Torrents.csv, BitSearch, Knaben and TheRARBG (its RSS search feed) use JSON/RSS APIs with their own parsers (seeders, size, age); the older sites are scraped for magnet and hash links. Many of the older sites are dead or blocked in some countries; they show as offline and are skipped.
- **Search suggestions** as you type: your recent searches first, then online suggestions. Up/Down to pick, Space takes a suggestion into the box so you can keep typing, Enter searches.
- **Result cards** with seeders, leechers, size, age and category. Click a card for the info hash, upload date, file count and a **file tree** with sizes.
- **Buttons per result:** open the magnet link, copy it, star it into Favorites, or save the `.torrent` file (when a public .torrent cache has it). On Android the `.torrent` link is handed to the browser's download manager.
- Sort by seeders, size, newest or name. Search history. Optional update check against GitHub releases (it only tells you; nothing is installed).

Start with a search from the command line: `SailHighSea-TorrentSearch.exe ubuntu 24.04`.

## Portable mode (desktop)

Put a file called `portable.txt` next to the program (or start it with `--portable`) and settings, favorites and history are kept in a `data/` folder next to it, including the WebView's own cache. The portable downloads already contain the marker; delete it to use the normal app-data folder.

"Next to the program" means: the folder of the `.exe` on Windows and Linux, the folder that holds the `.AppImage` file for AppImages, and the folder that holds the `.app` bundle on macOS. If that folder is read-only (Program Files, /Applications) the app falls back to the normal app-data folder and says so in Settings. Android has no portable mode.

Settings are `settings.json`, `favorites.json` and `history.json`. Files from the old C version (`torrentsearch-*.txt|tsv` next to the program) are imported on first run.

## Downloads

Each release has, per platform: Windows x64 and ARM64 (setup `.exe` and a portable `.zip`), Linux x64 (`.AppImage`, `.deb` and a portable `.tar.gz`), macOS arm64 (`.dmg` and a portable `.zip`) and an Android `.apk`. The macOS and Android builds are not notarised or Play-signed; macOS asks you to right-click > Open the first time, and Android needs "install unknown apps".

## Build

You need Node 22+, Rust (stable) and the Tauri 2 prerequisites for your OS (on Windows: MSVC Build Tools; on Linux: `libwebkit2gtk-4.1-dev` and friends).

```bash
npm install
npm run tauri dev          # run with hot reload
npm run tauri build        # installers + binary in src-tauri/target/release
cargo test --manifest-path src-tauri/Cargo.toml --lib     # engine, store and portable-mode tests (no network)
```

The UI can also be opened in a plain browser with `npm run dev`; a small mock backend (`src/mock.ts`) answers when Tauri is not there.

A live check of all sources (needs the network, not part of the normal tests):

```bash
cargo test --manifest-path src-tauri/Cargo.toml --lib live_sources -- --ignored --nocapture
```

Android: install the Android SDK + NDK and JDK 17, set `ANDROID_HOME` and `NDK_HOME`, add the four Rust targets, then `npx tauri android init`, `python scripts/android-patch.py` and `npx tauri android build --apk`. CI does exactly this (see `.github/workflows/build-release.yml`).

## Releases

`.github/workflows/build-release.yml` runs the tests, builds the desktop bundles and the Android APK and publishes a GitHub release:

- push to `main`: stable release (v0.0.1, v0.0.2, ...)
- push to `dev`: pre-release (v0.0.1-dev-1, ...)
- pull request or manual run: build and tests only

The Android release needs four repository secrets: `ANDROID_KEYSTORE_BASE64` (the keystore file, base64), `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD`. Without them CI builds a debug APK to prove the build and does not publish it.

## Project layout

- `src/` - React + TypeScript UI: `pages/`, `components/`, `store.ts` (Zustand), `api.ts` (Tauri commands), `themes.css` (the nine themes), `mock.ts` (browser preview)
- `src-tauri/src/engine/` - sources table, parsers, merge, magnet builder, bencode / file lists, HTTP (`reqwest` + rustls), update check. No Tauri types in here, so the tests run headless
- `src-tauri/src/store.rs` - settings, favorites and history files, import of the old C files
- `src-tauri/src/paths.rs` - where data lives, portable mode
- `src-tauri/src/commands.rs` - the commands the UI calls and the `search:update` events
- `scripts/next-version.sh` - release tag numbering; `scripts/android-patch.py` - tweaks to the generated Android project; `scripts/dev-env.ps1` - local tool paths for the maintainer's machine

## Development notes

- To add a site, add a row to the `SOURCES` table in `src-tauri/src/engine/mod.rs` and raise `SRC_COUNT` (at most 32, because sources are a bit mask in the settings).
- Build URLs with `url_encode`; `http_get` rejects unusual characters.
- Dev flags: `--type=text` fills the search box without searching, `--expand` opens the first result after a search.

MIT licensed.
