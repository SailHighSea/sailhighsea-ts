import { api } from "../api";
import { Scroller } from "../components/Scroller";
import { useApp } from "../store";
import { AboutBody } from "./AboutPage";
import { SORTS, THEMES, type SortKey, type ThemeName } from "../types";

function Switch({ label, hint, checked, onChange }: { label: string; hint?: string; checked: boolean; onChange: (v: boolean) => void }) {
  return (
    <label className="switch-row">
      <span>
        <strong>{label}</strong>
        {hint && <small className="muted">{hint}</small>}
      </span>
      <input type="checkbox" role="switch" checked={checked} onChange={(e) => onChange(e.target.checked)} />
    </label>
  );
}

export function SettingsPage() {
  const settings = useApp((s) => s.settings);
  const sources = useApp((s) => s.sources);
  const info = useApp((s) => s.info);
  const patch = useApp((s) => s.patchSettings);
  const checkUpdate = useApp((s) => s.checkUpdate);
  const showToast = useApp((s) => s.showToast);
  if (!settings) return null;

  const on = (i: number) => ((settings.sourceMask >>> i) & 1) === 1;
  const setSource = (i: number, v: boolean) => void patch({ sourceMask: (v ? settings.sourceMask | (1 << i) : settings.sourceMask & ~(1 << i)) >>> 0 });
  const allMask = (sources.length >= 32 ? 0xffffffff : (1 << sources.length) - 1) >>> 0;
  const defaults = sources.reduce((m, s, i) => (s.on ? (m | (1 << i)) >>> 0 : m), 0);

  return (
    <div className="page">
      <h1>Settings</h1>
      <Scroller>
        <div className="settings">
        <section className="panel">
          <h2>Appearance</h2>
          <div className="theme-grid" role="radiogroup" aria-label="Theme">
            {THEMES.map((t) => (
              <button key={t.id} role="radio" aria-checked={settings.theme === t.id} className={"theme-btn" + (settings.theme === t.id ? " active" : "")} onClick={() => void patch({ theme: t.id as ThemeName })}>
                {t.label}
              </button>
            ))}
          </div>
          <label className="row">
            <span>Default sort</span>
            <select value={settings.sort} onChange={(e) => void patch({ sort: e.target.value as SortKey })}>
              {SORTS.map((s) => <option key={s.id} value={s.id}>{s.label.replace("Sort by ", "")}</option>)}
            </select>
          </label>
        </section>

        <section className="panel">
          <h2>Sources <small className="muted">{sources.filter((_, i) => on(i)).length} of {sources.length} on</small></h2>
          <div className="row buttons">
            <button className="btn" onClick={() => void patch({ sourceMask: allMask })}>All</button>
            <button className="btn" onClick={() => void patch({ sourceMask: 0 })}>None</button>
            <button className="btn" onClick={() => void patch({ sourceMask: defaults })}>Defaults</button>
          </div>
          <p className="muted small">Many of the older sites are dead or blocked in some countries; they show as offline and are skipped.</p>
          <div className="source-list">
            {sources.map((s, i) => (
              <label key={s.name} className="check">
                <input type="checkbox" checked={on(i)} onChange={(e) => setSource(i, e.target.checked)} /> {s.name}
              </label>
            ))}
          </div>
        </section>

        <section className="panel">
          <h2>Updates</h2>
          <Switch label="Check for updates automatically" hint="Looks for a newer release on GitHub at start-up and tells you in the status bar" checked={settings.checkUpdates} onChange={(v) => void patch({ checkUpdates: v })} />
          <Switch label="Include dev builds" hint="Also tell me about pre-release dev builds (newer features, may be unstable)" checked={settings.updateDev} onChange={(v) => void patch({ updateDev: v })} />
          <div className="row buttons"><button className="btn" onClick={() => void checkUpdate(true)}>Check now</button></div>
        </section>

        <section className="panel">
          <h2>Data</h2>
          <p className="muted small">
            {info?.portable ? "Portable mode: settings, favorites and history are kept next to the program." : "Settings, favorites and history are kept in the app data folder."}
            {info?.portableFallback && " (Portable mode was requested, but the program folder is read-only.)"}
          </p>
          <p className="mono small wrap">{info?.dataDir}</p>
          {!info?.mobile && (
            <div className="row buttons">
              <button className="btn" onClick={() => api.openDataFolder().catch((e) => showToast(String(e)))}>Open folder</button>
            </div>
          )}
        </section>
        {info?.mobile && (
          <section className="panel">
            <AboutBody />
          </section>
        )}
        </div>
      </Scroller>
    </div>
  );
}
