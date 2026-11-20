import { useMemo, useState } from "react";
import logo from "../assets/icon.svg";
import { ResultList } from "../components/ResultList";
import { Scroller } from "../components/Scroller";
import { SearchBox } from "../components/SearchBox";
import { sortResults, useApp } from "../store";
import { SORTS, type SortKey } from "../types";

export function SearchPage() {
  const results = useApp((s) => s.results);
  const sort = useApp((s) => s.settings?.sort ?? "seeders");
  const patchSettings = useApp((s) => s.patchSettings);
  const sourceViews = useApp((s) => s.sourceViews);
  const sources = useApp((s) => s.sources);
  const searched = useApp((s) => s.searched);
  const busy = useApp((s) => s.busy);
  const searchedQuery = useApp((s) => s.searchedQuery);
  const history = useApp((s) => s.history);
  const startSearch = useApp((s) => s.startSearch);
  const asked = sourceViews.filter((s) => s.state !== "idle").length;
  const answered = sourceViews.filter((s) => s.state === "ok" || s.state === "failed").length;
  const [showSources, setShowSources] = useState(false);
  const sorted = useMemo(() => sortResults(results, sort), [results, sort]);

  return (
    <div className="page search-page">
      <SearchBox />
      {busy && <div className="progress" role="progressbar" aria-label="Searching"><i style={{ width: `${Math.max(8, asked ? (answered / asked) * 100 : 0)}%` }} /></div>}
      {searched && (
        <div className="toolbar">
          <span className="muted">{busy ? "Searching" : "Results"} for &ldquo;{searchedQuery}&rdquo;</span>
          <button className="link" onClick={() => setShowSources(!showSources)}>{showSources ? "Hide sources" : "Sources"}</button>
          <select value={sort} onChange={(e) => void patchSettings({ sort: e.target.value as SortKey })} aria-label="Sort">
            {SORTS.map((s) => <option key={s.id} value={s.id}>{s.label}</option>)}
          </select>
        </div>
      )}
      {showSources && (
        <div className="source-grid">
          {sourceViews.map((v, i) => v.state !== "idle" && (
            <span key={i} className={"source-chip " + v.state}>
              {sources[i]?.name}: {v.state === "running" ? "..." : v.state === "failed" ? "offline" : v.count}
            </span>
          ))}
        </div>
      )}
      <Scroller>
        {searched ? (
          <ResultList items={sorted} empty={busy ? "Searching..." : "Nothing found. Try other words or turn on more sources in Settings."} />
        ) : (
          <div className="empty hero">
            <img src={logo} alt="" width={72} height={72} />
            <h2>Search public torrent indexes</h2>
            <p>Results from many sites are merged by info-hash. Open a magnet link in your own torrent app, or save it to Favorites.</p>
            {history.length > 0 && (
              <div className="hero-chips">
                {history.slice(0, 6).map((q) => <button key={q} className="chip-btn" onClick={() => void startSearch(q)}>{q}</button>)}
              </div>
            )}
          </div>
        )}
      </Scroller>
    </div>
  );
}
