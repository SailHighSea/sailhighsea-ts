import { api } from "../api";
import { useApp } from "../store";

export function StatusBar() {
  const { busy, results, sourceViews, searched, update, toast, info } = useApp();
  const answered = sourceViews.filter((s) => s.state === "ok" || s.state === "failed").length;
  const total = sourceViews.filter((s) => s.state !== "idle").length;

  let text = "Ready";
  if (busy) text = `Searching... ${answered}/${total} sources answered, ${results.length} results`;
  else if (searched) text = `${results.length} results from ${sourceViews.filter((s) => s.state === "ok").length} of ${total} sources`;

  return (
    <footer className="statusbar">
      <span className="status-text">{toast || text}</span>
      {update && (
        <button className="status-update" onClick={() => void api.openUrl(update.url)}>
          Update available: {update.tag}
        </button>
      )}
      <span className="status-version">v{info?.version}</span>
    </footer>
  );
}
