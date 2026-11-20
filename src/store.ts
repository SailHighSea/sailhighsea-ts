import { create } from "zustand";
import { api } from "./api";
import type { AppInfo, Detail, Result, SearchUpdate, Settings, SortKey, SourceInfo, SourceView, UpdateInfo } from "./types";

export type Page = "search" | "favorites" | "history" | "settings" | "about";

interface State {
  ready: boolean;
  info: AppInfo | null;
  settings: Settings | null;
  sources: SourceInfo[];
  page: Page;
  navHidden: boolean; // the floating pill slides away while scrolling down

  // search
  query: string;
  searchedQuery: string;
  searched: boolean;
  busy: boolean;
  results: Result[];
  sourceViews: SourceView[];

  favorites: Result[];
  history: string[];
  details: Record<string, Detail>;
  openHash: string | null;

  update: UpdateInfo | null;
  toast: string;

  init: () => Promise<void>;
  setPage: (p: Page) => void;
  setNavHidden: (h: boolean) => void;
  setQuery: (q: string) => void;
  startSearch: (q?: string) => Promise<void>;
  patchSettings: (p: Partial<Settings>) => Promise<void>;
  toggleFavorite: (r: Result) => Promise<void>;
  openCard: (r: Result | null) => void;
  openMagnet: (r: Result) => Promise<void>;
  copyMagnet: (r: Result) => Promise<void>;
  saveTorrent: (r: Result) => Promise<void>;
  removeHistory: (q: string) => Promise<void>;
  clearHistory: () => Promise<void>;
  checkUpdate: (manual: boolean) => Promise<void>;
  showToast: (msg: string) => void;
}

let toastTimer: ReturnType<typeof setTimeout> | undefined;
let maxGen = 0;
let expandFirst = false;

const msg = (e: unknown) => (typeof e === "string" ? e : e instanceof Error ? e.message : String(e));

export const useApp = create<State>((set, get) => ({
  ready: false,
  info: null,
  settings: null,
  sources: [],
  page: "search",
  navHidden: false,
  query: "",
  searchedQuery: "",
  searched: false,
  busy: false,
  results: [],
  sourceViews: [],
  favorites: [],
  history: [],
  details: {},
  openHash: null,
  update: null,
  toast: "",

  showToast: (m) => {
    set({ toast: m });
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => set({ toast: "" }), 3500);
  },

  init: async () => {
    const [info, settings, sources, favorites, history] = await Promise.all([
      api.appInfo(), api.getSettings(), api.sources(), api.favorites(), api.history(),
    ]);
    set({ info, settings, sources, favorites, history, ready: true });

    await api.onSearchUpdate((u: SearchUpdate) => {
      if (u.gen < maxGen) return; // a late answer of an abandoned search
      maxGen = u.gen;
      set({ results: u.results, sourceViews: u.sources, busy: u.busy, searchedQuery: u.query });
      if (expandFirst && !u.busy && u.results.length) {
        expandFirst = false; // dev flag --expand: open the first result once the search is done
        get().openCard(sortResults(u.results, get().settings?.sort ?? "seeders")[0]);
      }
    });

    const { launch } = info;
    expandFirst = launch.expand;
    if (launch.query) {
      set({ query: launch.query });
      if (!launch.typeOnly) await get().startSearch(launch.query);
    }
    if (settings.checkUpdates) void get().checkUpdate(false);
  },

  setPage: (page) => set({ page, openHash: null, navHidden: false }),
  setNavHidden: (navHidden) => set((s) => (s.navHidden === navHidden ? s : { navHidden })),
  setQuery: (query) => set({ query }),

  startSearch: async (q) => {
    const query = (q ?? get().query).trim();
    if (!query) return;
    const settings = get().settings;
    if (!settings) return;
    set({ query, searched: true, page: "search", openHash: null, results: [], busy: true });
    try {
      await api.search(query, settings.sourceMask);
      set({ history: await api.history() });
    } catch (e) {
      set({ busy: false });
      get().showToast(msg(e));
    }
  },

  patchSettings: async (p) => {
    const cur = get().settings;
    if (!cur) return;
    const next = { ...cur, ...p };
    set({ settings: next });
    try {
      set({ settings: await api.setSettings(next) });
    } catch (e) {
      get().showToast("Could not save settings: " + msg(e));
    }
  },

  toggleFavorite: async (r) => {
    try {
      const added = await api.toggleFavorite(r);
      set({ favorites: await api.favorites() });
      get().showToast(added ? "Added to Favorites" : "Removed from Favorites");
    } catch (e) {
      get().showToast(msg(e));
    }
  },

  openCard: (r) => {
    if (!r || get().openHash === r.hash) {
      set({ openHash: null });
      return;
    }
    set({ openHash: r.hash });
    if (get().details[r.hash]) return;
    set((s) => ({ details: { ...s.details, [r.hash]: { hash: r.hash, state: "running", fileCount: 0, shown: 0, total: 0, files: [] } } }));
    api.detail(r.hash, r.tid)
      .then((d) => set((s) => ({ details: { ...s.details, [r.hash]: d } })))
      .catch(() => set((s) => ({ details: { ...s.details, [r.hash]: { ...s.details[r.hash], state: "failed" } } })))
      ;
  },

  openMagnet: async (r) => {
    try {
      await api.openUrl(r.magnet);
    } catch (e) {
      get().showToast("Could not open a torrent app: " + msg(e));
    }
  },

  copyMagnet: async (r) => {
    try {
      await api.copy(r.magnet);
      get().showToast("Magnet link copied");
    } catch (e) {
      get().showToast("Could not copy: " + msg(e));
    }
  },

  saveTorrent: async (r) => {
    get().showToast("Looking for a .torrent file...");
    try {
      get().showToast(await api.saveTorrent(r.hash, r.name));
    } catch (e) {
      get().showToast(msg(e));
    }
  },

  removeHistory: async (q) => set({ history: await api.historyRemove(q) }),
  clearHistory: async () => {
    await api.historyClear();
    set({ history: [] });
  },

  checkUpdate: async (manual) => {
    const s = get().settings;
    try {
      const u = await api.checkUpdate(!!s?.updateDev);
      set({ update: u && u.newer ? u : null });
      if (manual) get().showToast(!u ? "Could not check for updates" : u.newer ? `Update available: ${u.tag}` : "You have the latest version");
    } catch {
      if (manual) get().showToast("Could not check for updates");
    }
  },
}));

export function sortResults(list: Result[], key: SortKey): Result[] {
  const out = [...list];
  out.sort((a, b) => {
    switch (key) {
      case "size":
        if (a.size !== b.size) return b.size - a.size;
        break;
      case "newest":
        if (a.added !== b.added) return b.added - a.added;
        break;
      case "name": {
        const c = a.name.localeCompare(b.name);
        if (c) return c;
        break;
      }
      default:
        if (a.seeders !== b.seeders) return b.seeders - a.seeders;
    }
    return b.leechers - a.leechers;
  });
  return out;
}
