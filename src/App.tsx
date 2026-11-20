import { useEffect } from "react";
import { Rail } from "./components/Rail";
import { StatusBar } from "./components/StatusBar";
import { TitleBar } from "./components/TitleBar";
import { AboutPage } from "./pages/AboutPage";
import { FavoritesPage } from "./pages/FavoritesPage";
import { HistoryPage } from "./pages/HistoryPage";
import { SearchPage } from "./pages/SearchPage";
import { SettingsPage } from "./pages/SettingsPage";
import { inTauri } from "./api";
import { useApp } from "./store";

/** "auto" follows the OS light/dark setting; the other themes are used as they are. */
function useTheme() {
  const theme = useApp((s) => s.settings?.theme ?? "auto");
  useEffect(() => {
    const mq = window.matchMedia("(prefers-color-scheme: dark)");
    const apply = () => {
      const t = theme === "auto" ? (mq.matches ? "dark" : "light") : theme;
      document.documentElement.dataset.theme = t;
      document.documentElement.dataset.look = t === "light" || t === "dark" || t === "glass" ? "modern" : "retro"; // the retro themes keep their classic chrome
    };
    apply();
    if (theme !== "auto") return;
    mq.addEventListener("change", apply);
    return () => mq.removeEventListener("change", apply);
  }, [theme]);
}

/** The hidden navigation pill comes back when the pointer goes near the bottom edge (desktop). */
function useNavReveal() {
  useEffect(() => {
    const onMove = (e: MouseEvent) => {
      if (e.clientY > window.innerHeight - 110) useApp.getState().setNavHidden(false);
    };
    window.addEventListener("mousemove", onMove);
    return () => window.removeEventListener("mousemove", onMove);
  }, []);
}

/** Android back button: close the open card, then go back to Search, then leave the app. */
function useBackButton() {
  useEffect(() => {
    if (!inTauri) return;
    let off: (() => void) | undefined;
    void (async () => {
      try {
        const { onBackButtonPress } = await import("@tauri-apps/api/app");
        const l = await onBackButtonPress(async () => {
          const s = useApp.getState();
          if (s.openHash) s.openCard(null);
          else if (s.page !== "search") s.setPage("search");
          else (await import("@tauri-apps/api/window")).getCurrentWindow().close();
        });
        off = () => void l.unregister();
      } catch {
        /* desktop: no back button */
      }
    })();
    return () => off?.();
  }, []);
}

export default function App() {
  const ready = useApp((s) => s.ready);
  const page = useApp((s) => s.page);
  const mobile = useApp((s) => s.info?.mobile ?? false);
  const init = useApp((s) => s.init);

  useEffect(() => {
    void init();
  }, [init]);
  useTheme();
  useBackButton();
  useNavReveal();

  if (!ready) return <div className="splash">Loading...</div>;
  return (
    <div className={"app" + (mobile ? " mobile" : "")}>
      {inTauri && !mobile && <TitleBar />}
      <div className="body">
        <Rail />
        <main>
          {page === "search" && <SearchPage />}
          {page === "favorites" && <FavoritesPage />}
          {page === "history" && <HistoryPage />}
          {page === "settings" && <SettingsPage />}
          {page === "about" && <AboutPage />}
        </main>
      </div>
      <StatusBar />
    </div>
  );
}
