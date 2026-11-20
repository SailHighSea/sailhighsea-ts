import { getCurrentWindow } from "@tauri-apps/api/window";
import logo from "../assets/icon.svg";
import { Icon } from "./Icon";

/** Custom title bar for desktop (the native one is switched off). Dragging works through data-tauri-drag-region. */
export function TitleBar() {
  const win = getCurrentWindow();
  return (
    <header className="titlebar" data-tauri-drag-region onDoubleClick={() => void win.toggleMaximize()}>
      <img src={logo} alt="" className="titlebar-logo" data-tauri-drag-region />
      <span className="titlebar-title" data-tauri-drag-region>SailHighSea TorrentSearch</span>
      <div className="titlebar-buttons">
        <button aria-label="Minimize" onClick={() => void win.minimize()}><Icon name="minimize" size={14} /></button>
        <button aria-label="Maximize" onClick={() => void win.toggleMaximize()}><Icon name="maximize" size={12} /></button>
        <button aria-label="Close" className="close" onClick={() => void win.close()}><Icon name="close" size={14} /></button>
      </div>
    </header>
  );
}
