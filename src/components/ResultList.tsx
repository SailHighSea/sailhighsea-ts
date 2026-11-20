import { useApp } from "../store";
import type { Result } from "../types";
import { ResultCard } from "./ResultCard";

export function ResultList({ items, empty }: { items: Result[]; empty: string }) {
  const openHash = useApp((s) => s.openHash);
  const sources = useApp((s) => s.sources);
  const favorites = useApp((s) => s.favorites);
  const starred = new Set(favorites.map((f) => f.hash));
  if (!items.length) return <div className="empty">{empty}</div>;
  return (
    <div className="list">
      {items.map((r) => (
        <ResultCard key={r.hash} r={r} open={openHash === r.hash} starred={starred.has(r.hash)} sourceName={sources[r.source]?.name ?? "?"} />
      ))}
    </div>
  );
}
