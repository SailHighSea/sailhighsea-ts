import { useApp, type Page } from "../store";
import { Icon, type IconName } from "./Icon";

const ALL_ITEMS: { id: Page; label: string; icon: IconName }[] = [
  { id: "search", label: "Search", icon: "search" },
  { id: "favorites", label: "Favorites", icon: "star" },
  { id: "history", label: "History", icon: "history" },
  { id: "settings", label: "Settings", icon: "settings" },
  { id: "about", label: "About", icon: "info" },
];

/** Icon rail on wide windows, bottom navigation bar on narrow ones (see the media query in app.css). */
export function Rail() {
  const navHidden = useApp((s) => s.navHidden);
  const mobile = useApp((s) => s.info?.mobile ?? false);
  const ITEMS = mobile ? ALL_ITEMS.filter((i) => i.id !== "about") : ALL_ITEMS; // on a phone, About lives at the bottom of Settings
  const page = useApp((s) => s.page);
  const setPage = useApp((s) => s.setPage);
  const favCount = useApp((s) => s.favorites.length);
  return (
    <nav className={"rail" + (navHidden ? " nav-hidden" : "")} aria-label="Pages">
      {ITEMS.map((it) => (
        <button key={it.id} className={"rail-item" + (page === it.id ? " active" : "")} onClick={() => setPage(it.id)} title={it.label} aria-label={it.label} aria-current={page === it.id}>
          <Icon name={it.icon} size={22} />
          <span>{it.label}</span>
          {it.id === "favorites" && favCount > 0 && <em className="badge">{favCount}</em>}
        </button>
      ))}
    </nav>
  );
}
