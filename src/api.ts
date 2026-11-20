// Thin wrappers around the Rust commands. In a plain browser (npm run dev without Tauri) a small mock answers instead,
// so the UI can be previewed and tested without the backend.
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { writeText } from "@tauri-apps/plugin-clipboard-manager";
import type { AppInfo, Detail, Result, SearchUpdate, Settings, SourceInfo, UpdateInfo } from "./types";
import * as mock from "./mock";

export const inTauri = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

function call<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  return inTauri ? invoke<T>(cmd, args) : (mock.invoke(cmd, args) as Promise<T>);
}

export const api = {
  appInfo: () => call<AppInfo>("app_info"),
  sources: () => call<SourceInfo[]>("sources_list"),
  getSettings: () => call<Settings>("settings_get"),
  setSettings: (settings: Settings) => call<Settings>("settings_set", { settings }),
  favorites: () => call<Result[]>("favorites_get"),
  toggleFavorite: (result: Result) => call<boolean>("fav_toggle", { result }),
  history: () => call<string[]>("history_get"),
  historyAdd: (query: string) => call<string[]>("history_add", { query }),
  historyRemove: (query: string) => call<string[]>("history_remove", { query }),
  historyClear: () => call<void>("history_clear"),
  search: (query: string, mask: number) => call<number>("search", { query, mask }),
  suggest: (query: string) => call<string[]>("suggest", { query }),
  detail: (hash: string, tid: number) => call<Detail>("detail", { hash, tid }),
  saveTorrent: (hash: string, name: string) => call<string>("save_torrent", { hash, name }),
  openUrl: (url: string) => call<void>("open_url", { url }),
  openDataFolder: () => call<void>("open_data_folder"),
  checkUpdate: (includeDev: boolean) => call<UpdateInfo | null>("check_update", { includeDev }),
  copy: async (text: string) => {
    if (inTauri) await writeText(text);
    else await navigator.clipboard.writeText(text);
  },
  onSearchUpdate: (fn: (u: SearchUpdate) => void): Promise<() => void> =>
    inTauri ? listen<SearchUpdate>("search:update", (e) => fn(e.payload)) : mock.onSearchUpdate(fn),
};
