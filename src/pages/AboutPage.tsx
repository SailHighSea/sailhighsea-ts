import { api } from "../api";
import logo from "../assets/icon.svg";
import { Icon } from "../components/Icon";
import { Scroller } from "../components/Scroller";
import { useApp } from "../store";

const REPO = "https://github.com/SailHighSea/sailhighsea-ts";

export function AboutBody() {
  const info = useApp((s) => s.info);
  const showToast = useApp((s) => s.showToast);
  const checkUpdate = useApp((s) => s.checkUpdate);
  if (!info) return null;
  const rows: [string, string][] = [
    ["Version", info.version],
    ["Platform", `${info.platform} (${info.arch})`],
    ["Mode", info.mobile ? "Mobile" : info.portable ? "Portable" : "Installed"],
    ["License", "MIT License"],
    ["Copyright", "(c) 2026 SailHighSea contributors"],
    ["Website", "github.com/SailHighSea/sailhighsea-ts"],
    ["Data folder", info.dataDir],
  ];
  const summary = rows.map(([k, v]) => `${k}: ${v}`).join("\n");
  return (
    <div className="about">
        <div className="about-head">
          <img src={logo} alt="" width={96} height={96} />
          <div>
            <h1>SailHighSea TorrentSearch</h1>
            <div className="accent">Version {info.version}</div>
            <p className="muted">A tiny, portable, open-source torrent search tool.</p>
          </div>
        </div>
        <p className="muted">Searches public torrent indexes and gives you the magnet link, ready for your own torrent app.</p>
        <p className="muted">It never downloads or shares files itself. Star a result to keep it in Favorites.</p>
        <p className="muted">Only download material you have the right to. Search results come from third-party sites.</p>
        <dl className="about-rows panel">
          {rows.map(([k, v]) => (
            <div key={k}><dt>{k}</dt><dd className="wrap">{v}</dd></div>
          ))}
        </dl>
        <div className="row buttons">
          <button className="btn primary" onClick={() => void api.openUrl(REPO).catch((e) => showToast(String(e)))}><Icon name="info" /> <span>GitHub page</span></button>
          <button className="btn" onClick={() => void checkUpdate(true)}>Check for updates</button>
          <button className="btn" onClick={() => void api.copy(summary).then(() => showToast("Copied"))}><Icon name="copy" /> <span>Copy info</span></button>
        </div>
    </div>
  );
}

export function AboutPage() {
  return (
    <div className="page">
      <Scroller>
        <AboutBody />
      </Scroller>
    </div>
  );
}
