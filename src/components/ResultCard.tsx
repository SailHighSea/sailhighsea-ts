import { memo } from "react";
import { formatAge, formatDate, formatSize } from "../format";
import { useApp } from "../store";
import type { Result } from "../types";
import { FileTree } from "./FileTree";
import { Icon } from "./Icon";

interface Props {
  r: Result;
  open: boolean;
  starred: boolean;
  sourceName: string;
}

function Card({ r, open, starred, sourceName }: Props) {
  const { openCard, openMagnet, copyMagnet, toggleFavorite, saveTorrent } = useApp.getState();
  const detail = useApp((s) => (open ? s.details[r.hash] : undefined));
  const stop = (fn: () => void) => (e: React.MouseEvent) => {
    e.stopPropagation();
    fn();
  };
  return (
    <article className={"card" + (open ? " open" : "")} onClick={() => openCard(r)}>
      <div className="card-main">
        <div className="card-title" title={r.name}>{r.name}</div>
        <div className="card-meta">
          <span className="src">{sourceName}</span>
          {r.detail && <span className="chip">{r.detail}</span>}
          <span className="seeds" title="Seeders">&uarr; {r.seeders}</span>
          <span className="leech" title="Leechers">&darr; {r.leechers}</span>
          <span>{formatSize(r.size)}</span>
          <span>{formatAge(r.added)}</span>
        </div>
      </div>
      <div className="card-actions">
        <button className="btn primary" onClick={stop(() => void openMagnet(r))} title="Open the magnet link in your torrent app">
          <Icon name="magnet" /> <span>Open</span>
        </button>
        <button className="btn" onClick={stop(() => void copyMagnet(r))} title="Copy the magnet link"><Icon name="copy" /> <span>Copy</span></button>
        <button className={"btn" + (starred ? " on" : "")} onClick={stop(() => void toggleFavorite(r))} title={starred ? "Remove from Favorites" : "Add to Favorites"}>
          <Icon name="star" />
        </button>
        <button className="btn" onClick={stop(() => void saveTorrent(r))} title="Save the .torrent file (when a public cache has it)"><Icon name="download" /></button>
      </div>
      {open && (
        <div className="card-detail" onClick={(e) => e.stopPropagation()}>
          <dl>
            <dt>Info hash</dt><dd className="mono">{r.hash}</dd>
            <dt>Uploaded</dt><dd>{formatDate(r.added)}</dd>
            {r.files > 0 && (<><dt>Files</dt><dd>{r.files}</dd></>)}
          </dl>
          {detail && <FileTree detail={detail} />}
        </div>
      )}
    </article>
  );
}

export const ResultCard = memo(Card);
