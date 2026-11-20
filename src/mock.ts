// Browser-only stand-in for the Rust backend (used when the page is opened without Tauri).
import type { AppInfo, Detail, Result, SearchUpdate, Settings, SourceInfo } from "./types";

const NAMES = ["Pirate Bay", "YTS", "Nyaa", "Sukebei", "1337x", "TorrentGalaxy", "LimeTorrents", "Torlock", "EZTV", "ETTV", "KickassTorrents", "Zooqle",
  "TorrentKitty", "Torrentz2", "BTDB", "BTSOW", "SkyTorrents", "SeedPeer", "ExtraTorrent", "Monova", "TheHiddenBay", "BTDigg", "BTBaocai", "BTZhizhu",
  "Diaosisou", "NMS000", "Aliciliba", "Dabaicai", "Torrents.csv", "BitSearch", "Knaben", "TheRARBG"];

const load = <T,>(k: string, d: T): T => {
  try {
    const v = localStorage.getItem("mock:" + k);
    return v ? (JSON.parse(v) as T) : d;
  } catch {
    return d;
  }
};
const save = (k: string, v: unknown) => {
  try {
    localStorage.setItem("mock:" + k, JSON.stringify(v));
  } catch {
    /* ignore */
  }
};

let settings: Settings = load("settings", { theme: "auto", sourceMask: 0xffffffef, sourceCount: 32, sort: "seeders", checkUpdates: false, updateDev: false });
let favorites: Result[] = load("fav", []);
let history: string[] = load("hist", ["ubuntu", "big buck bunny"]);
let listener: ((u: SearchUpdate) => void) | null = null;
let gen = 0;

const hex = (n: number) => n.toString(16).padStart(40, "0");

function fakeResults(q: string): Result[] {
  const kinds = ["1080p BluRay x265", "720p WEB-DL", "2160p HDR", "DVDRip", "Complete Collection", "[Group] 01-12 Batch", "FLAC", "ISO", "Portable"];
  return kinds.map((k, i) => ({
    name: `${q} ${k}`,
    hash: hex(0xabc000 + i * 7919 + q.length),
    detail: ["HD Movies", "TV shows", "Anime", "Applications"][i % 4],
    source: [0, 1, 2, 28, 29, 30, 4, 5, 6][i],
    seeders: 900 - i * 97,
    leechers: 40 - i * 3,
    size: (i + 1) * 410_000_000,
    added: Math.floor(Date.now() / 1000) - i * 86400 * 9,
    tid: i === 0 ? 1 : 0,
    files: i + 1,
    magnet: `magnet:?xt=urn:btih:${hex(0xabc000 + i * 7919 + q.length)}&dn=${encodeURIComponent(q)}`,
  }));
}

export function onSearchUpdate(fn: (u: SearchUpdate) => void) {
  listener = fn;
  return Promise.resolve(() => {
    listener = null;
  });
}

export async function invoke(cmd: string, a: Record<string, unknown> = {}): Promise<unknown> {
  switch (cmd) {
    case "app_info":
      return {
        version: "0.0.1-preview", platform: "browser", arch: "-", portable: false, portableFallback: false, dataDir: "(browser preview)", mobile: location.search.includes("mobile"),
        launch: { query: "", typeOnly: false, expand: false },
      } satisfies AppInfo;
    case "sources_list":
      return NAMES.map((name, i) => ({ name, on: i !== 3 })) satisfies SourceInfo[];
    case "settings_get":
      return settings;
    case "settings_set":
      settings = a.settings as Settings;
      save("settings", settings);
      return settings;
    case "favorites_get":
      return favorites;
    case "fav_toggle": {
      const r = a.result as Result;
      const i = favorites.findIndex((f) => f.hash === r.hash);
      if (i >= 0) favorites.splice(i, 1);
      else favorites.unshift(r);
      save("fav", favorites);
      return i < 0;
    }
    case "history_get":
      return history;
    case "history_add":
      history = [a.query as string, ...history.filter((h) => h !== a.query)].slice(0, 50);
      save("hist", history);
      return history;
    case "history_remove":
      history = history.filter((h) => h !== a.query);
      save("hist", history);
      return history;
    case "history_clear":
      history = [];
      save("hist", history);
      return;
    case "search": {
      const q = a.query as string;
      const my = ++gen;
      const mask = a.mask as number;
      const sources = NAMES.map((_, i) => ({ state: ((mask >>> i) & 1 ? "running" : "idle") as "running" | "idle", count: 0 }));
      listener?.({ gen: my, query: q, busy: true, results: [], sources: sources.map((s) => ({ ...s })) });
      setTimeout(() => {
        if (my !== gen) return;
        const results = fakeResults(q);
        sources.forEach((s, i) => {
          if (s.state === "running") {
            s.state = i === 3 || i === 9 ? ("failed" as never) : ("ok" as never);
            s.count = s.state === "ok" ? 1 : 0;
          }
        });
        listener?.({ gen: my, query: q, busy: false, results, sources });
      }, 600);
      return my;
    }
    case "suggest":
      return [`${a.query} 2024`, `${a.query} download`, `${a.query} iso`];
    case "detail": {
      const hash = a.hash as string;
      const files = [
        { path: "Release/video.mkv", size: 1_900_000_000 },
        { path: "Release/subs/en.srt", size: 90_000 },
        { path: "Release/subs/de.srt", size: 88_000 },
        { path: "Release/readme.nfo", size: 2_000 },
      ];
      return { hash, state: "ok", fileCount: files.length, shown: files.length, total: files.reduce((s, f) => s + f.size, 0), files } satisfies Detail;
    }
    case "save_torrent":
      return "Saved C:\\Users\\you\\Downloads\\example.torrent";
    case "open_url":
      return;
    case "open_data_folder":
      throw "Not available in the browser preview";
    case "check_update":
      return null;
    default:
      throw `unknown command ${cmd}`;
  }
}
