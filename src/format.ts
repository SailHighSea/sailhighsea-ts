export function formatSize(b: number): string {
  const GB = 1073741824;
  if (b <= 0) return "-";
  if (b >= GB * 1024) return (b / (GB * 1024)).toFixed(2) + " TB";
  if (b >= GB) return (b / GB).toFixed(2) + " GB";
  if (b >= 1048576) return Math.round(b / 1048576) + " MB";
  if (b >= 1024) return Math.round(b / 1024) + " KB";
  return b + " B";
}

export function formatAge(then: number, now = Date.now() / 1000): string {
  if (then <= 0) return "-";
  const d = Math.max(0, now - then);
  if (d < 3600) return Math.floor(d / 60) + "m ago";
  if (d < 86400) return Math.floor(d / 3600) + "h ago";
  if (d < 86400 * 60) return Math.floor(d / 86400) + "d ago";
  if (d < 86400 * 730) return Math.floor(d / (86400 * 30)) + "mo ago";
  return Math.floor(d / (86400 * 365)) + "y ago";
}

export function formatDate(unix: number): string {
  return unix > 0 ? new Date(unix * 1000).toISOString().slice(0, 10) : "-";
}
