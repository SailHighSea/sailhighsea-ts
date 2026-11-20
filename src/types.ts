export interface Result {
  name: string;
  hash: string;
  detail: string;
  source: number;
  seeders: number;
  leechers: number;
  size: number;
  added: number;
  tid: number;
  files: number;
  magnet: string;
}

export type SourceState = "idle" | "running" | "ok" | "failed";
export interface SourceView {
  state: SourceState;
  count: number;
}

export interface SearchUpdate {
  gen: number;
  query: string;
  busy: boolean;
  results: Result[];
  sources: SourceView[];
}

export type ThemeName = "auto" | "light" | "dark" | "glass" | "flat98" | "win98" | "mac7" | "platinum" | "cde" | "gnome2";
export type SortKey = "seeders" | "size" | "newest" | "name";

export interface Settings {
  theme: ThemeName;
  sourceMask: number;
  sourceCount: number;
  sort: SortKey;
  checkUpdates: boolean;
  updateDev: boolean;
}

export interface FileEntry {
  path: string;
  size: number;
}
export interface Detail {
  hash: string;
  state: "running" | "ok" | "failed";
  fileCount: number;
  shown: number;
  total: number;
  files: FileEntry[];
}

export interface LaunchArgs {
  query: string;
  typeOnly: boolean;
  expand: boolean;
}

export interface AppInfo {
  version: string;
  platform: string;
  arch: string;
  portable: boolean;
  portableFallback: boolean;
  dataDir: string;
  mobile: boolean;
  launch: LaunchArgs;
}

export interface SourceInfo {
  name: string;
  on: boolean;
}

export interface UpdateInfo {
  tag: string;
  url: string;
  newer: boolean;
}

export const THEMES: { id: ThemeName; label: string }[] = [
  { id: "auto", label: "System" },
  { id: "light", label: "Light" },
  { id: "dark", label: "Dark" },
  { id: "glass", label: "Frosted Glass" },
  { id: "flat98", label: "Flat 98" },
  { id: "win98", label: "Win98" },
  { id: "mac7", label: "Mac System 7" },
  { id: "platinum", label: "Mac Platinum" },
  { id: "cde", label: "Linux CDE" },
  { id: "gnome2", label: "Linux GNOME 2" },
];

export const SORTS: { id: SortKey; label: string }[] = [
  { id: "seeders", label: "Sort by Seeders" },
  { id: "size", label: "Sort by Size" },
  { id: "newest", label: "Sort by Newest" },
  { id: "name", label: "Sort by Name" },
];
