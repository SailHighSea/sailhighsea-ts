import { useMemo } from "react";
import { formatSize } from "../format";
import type { Detail } from "../types";
import { Icon } from "./Icon";

interface Node {
  name: string;
  size: number;
  dirs: Map<string, Node>;
  files: { name: string; size: number }[];
}

function build(files: Detail["files"]): Node {
  const root: Node = { name: "", size: 0, dirs: new Map(), files: [] };
  for (const f of files) {
    const parts = f.path.split("/");
    let n = root;
    n.size += Math.max(0, f.size);
    for (const p of parts.slice(0, -1)) {
      let d = n.dirs.get(p);
      if (!d) n.dirs.set(p, (d = { name: p, size: 0, dirs: new Map(), files: [] }));
      d.size += Math.max(0, f.size);
      n = d;
    }
    n.files.push({ name: parts[parts.length - 1], size: f.size });
  }
  return root;
}

function Dir({ node, depth }: { node: Node; depth: number }) {
  return (
    <>
      {[...node.dirs.values()].map((d) => (
        <details key={d.name} open={depth < 2}>
          <summary>
            <Icon name="folder" size={14} /> <span className="tree-name">{d.name}</span>
            <span className="tree-size">{formatSize(d.size)}</span>
          </summary>
          <div className="tree-indent"><Dir node={d} depth={depth + 1} /></div>
        </details>
      ))}
      {node.files.map((f, i) => (
        <div key={i} className="tree-file">
          <Icon name="file" size={14} /> <span className="tree-name">{f.name}</span>
          <span className="tree-size">{formatSize(f.size)}</span>
        </div>
      ))}
    </>
  );
}

export function FileTree({ detail }: { detail: Detail }) {
  const root = useMemo(() => build(detail.files), [detail.files]);
  if (detail.state === "running") return <div className="muted">Loading file list...</div>;
  if (detail.state === "failed") return <div className="muted">No file list available for this one.</div>;
  return (
    <div className="tree">
      <div className="muted tree-head">
        {detail.fileCount} file{detail.fileCount === 1 ? "" : "s"}, {formatSize(detail.total)}
        {detail.fileCount > detail.shown && ` (first ${detail.shown} shown)`}
      </div>
      <div className="tree-box"><Dir node={root} depth={0} /></div>
    </div>
  );
}
