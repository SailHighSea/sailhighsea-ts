import { Icon } from "../components/Icon";
import { Scroller } from "../components/Scroller";
import { useApp } from "../store";

export function HistoryPage() {
  const history = useApp((s) => s.history);
  const startSearch = useApp((s) => s.startSearch);
  const removeHistory = useApp((s) => s.removeHistory);
  const clearHistory = useApp((s) => s.clearHistory);
  return (
    <div className="page">
      <div className="page-head">
        <h1>History</h1>
        {history.length > 0 && <button className="btn" onClick={() => void clearHistory()}><Icon name="trash" /> <span>Clear all</span></button>}
      </div>
      <Scroller>
        {history.length === 0 ? (
          <div className="empty">No searches yet.</div>
        ) : (
          <ul className="history">
            {history.map((q) => (
              <li key={q}>
                <button className="history-q" onClick={() => void startSearch(q)}><Icon name="history" size={16} /> {q}</button>
                <button className="btn icon-only" aria-label={`Remove ${q}`} onClick={() => void removeHistory(q)}><Icon name="close" size={14} /></button>
              </li>
            ))}
          </ul>
        )}
      </Scroller>
    </div>
  );
}
