import { useEffect, useRef, useState } from "react";
import { api } from "../api";
import { useApp } from "../store";
import { Icon } from "./Icon";

interface Sug {
  text: string;
  local: boolean;
}

/** Search field with a suggestion list: recent searches first, then what the web suggests.
 *  Up/Down pick, Space takes the picked suggestion into the box so you can keep typing, Enter searches, Esc closes. */
export function SearchBox() {
  const query = useApp((s) => s.query);
  const setQuery = useApp((s) => s.setQuery);
  const startSearch = useApp((s) => s.startSearch);
  const history = useApp((s) => s.history);
  const busy = useApp((s) => s.busy);
  const [online, setOnline] = useState<{ q: string; list: string[] }>({ q: "", list: [] });
  const [focused, setFocused] = useState(false);
  const [sel, setSel] = useState(-1);
  const inputRef = useRef<HTMLInputElement>(null);

  // online suggestions, after a short pause in typing; a newer request makes older answers irrelevant
  useEffect(() => {
    const q = query.trim();
    if (q.length < 2) return;
    let stale = false;
    const t = setTimeout(() => {
      api.suggest(q).then((list) => !stale && setOnline({ q, list })).catch(() => {});
    }, 250);
    return () => {
      stale = true;
      clearTimeout(t);
    };
  }, [query]);

  const q = query.trim().toLowerCase();
  const sugs: Sug[] = [];
  const add = (text: string, local: boolean) => {
    if (sugs.length < 8 && text.toLowerCase() !== q && !sugs.some((s) => s.text.toLowerCase() === text.toLowerCase())) sugs.push({ text, local });
  };
  history.filter((h) => !q || h.toLowerCase().includes(q)).slice(0, 4).forEach((h) => add(h, true));
  if (online.q.toLowerCase() === q) online.list.forEach((s) => add(s, false));
  const open = focused && sugs.length > 0;

  const run = (text: string) => {
    setSel(-1);
    inputRef.current?.blur();
    void startSearch(text);
  };

  const onKey = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "ArrowDown" && open) {
      e.preventDefault();
      setSel((sel + 1) % sugs.length);
    } else if (e.key === "ArrowUp" && open) {
      e.preventDefault();
      setSel((sel - 1 + sugs.length) % sugs.length);
    } else if (e.key === " " && open && sel >= 0) {
      e.preventDefault();
      setQuery(sugs[sel].text + " ");
      setSel(-1);
    } else if (e.key === "Enter") {
      run(open && sel >= 0 ? sugs[sel].text : query);
    } else if (e.key === "Escape") {
      setSel(-1);
      inputRef.current?.blur();
    }
  };

  return (
    <div className="searchbox">
      <div className="field">
        <Icon name="search" />
        <input
          ref={inputRef}
          value={query}
          placeholder="Search torrents..."
          autoComplete="off"
          autoCapitalize="off"
          spellCheck={false}
          enterKeyHint="search"
          onChange={(e) => {
            setQuery(e.target.value);
            setSel(-1);
          }}
          onKeyDown={onKey}
          onFocus={() => setFocused(true)}
          onBlur={() => setTimeout(() => setFocused(false), 120)}
        />
        {query && (
          <button className="clear" aria-label="Clear" onMouseDown={(e) => e.preventDefault()} onClick={() => { setQuery(""); inputRef.current?.focus(); }}>
            <Icon name="close" size={14} />
          </button>
        )}
      </div>
      <button className="btn primary go" disabled={!query.trim() || busy} onClick={() => run(query)}>Search</button>
      {open && (
        <ul className="sugs" role="listbox">
          {sugs.map((s, i) => (
            <li key={s.text} role="option" aria-selected={i === sel} className={i === sel ? "sel" : ""} onMouseDown={(e) => { e.preventDefault(); run(s.text); }} onMouseEnter={() => setSel(i)}>
              <Icon name={s.local ? "history" : "search"} size={15} /> {s.text}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
