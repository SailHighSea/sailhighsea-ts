import { useMemo } from "react";
import { ResultList } from "../components/ResultList";
import { Scroller } from "../components/Scroller";
import { sortResults, useApp } from "../store";

export function FavoritesPage() {
  const favorites = useApp((s) => s.favorites);
  const sort = useApp((s) => s.settings?.sort ?? "seeders");
  const sorted = useMemo(() => sortResults(favorites, sort), [favorites, sort]);
  return (
    <div className="page">
      <h1>Favorites</h1>
      <Scroller>
        <ResultList items={sorted} empty="Nothing saved yet. Click the star on a result to keep it here." />
      </Scroller>
    </div>
  );
}
