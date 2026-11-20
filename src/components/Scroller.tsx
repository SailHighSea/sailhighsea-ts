import { useCallback, useEffect, useRef, useState } from "react";
import { useApp } from "../store";

/** A scroll area with a floating overlay scrollbar: it takes no layout space, fades in while you scroll or move the
 *  pointer over the area, and fades out again after a moment. The native scrollbar is hidden; wheel, touch and keyboard
 *  scrolling are the browser's own. The thumb can be dragged. */
export function Scroller({ children, className = "" }: { children: React.ReactNode; className?: string }) {
  const box = useRef<HTMLDivElement>(null);
  const inner = useRef<HTMLDivElement>(null);
  const timer = useRef<number | undefined>(undefined);
  const drag = useRef<{ y: number; top: number } | null>(null);
  const lastTop = useRef(0);
  const [thumb, setThumb] = useState({ top: 0, h: 0 });
  const [show, setShow] = useState(false);

  const update = useCallback(() => {
    const el = box.current;
    if (!el) return;
    const { scrollTop, scrollHeight, clientHeight } = el;
    if (scrollHeight <= clientHeight + 1) {
      setThumb((t) => (t.h === 0 ? t : { top: 0, h: 0 }));
      return;
    }
    const h = Math.max(36, (clientHeight * clientHeight) / scrollHeight);
    const top = (scrollTop / (scrollHeight - clientHeight)) * (clientHeight - h - 4);
    setThumb({ top, h });
  }, []);

  const flash = useCallback(() => {
    setShow(true);
    window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => {
      if (!drag.current) setShow(false);
    }, 900);
  }, []);

  useEffect(() => {
    const el = box.current;
    if (!el) return;
    const ro = new ResizeObserver(update); // content or window size changed
    ro.observe(el);
    if (inner.current) ro.observe(inner.current);
    update();
    return () => {
      ro.disconnect();
      window.clearTimeout(timer.current);
    };
  }, [update]);

  const onThumbDown = (e: React.PointerEvent<HTMLDivElement>) => {
    const el = box.current;
    if (!el) return;
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    drag.current = { y: e.clientY, top: el.scrollTop };
    setShow(true);
  };
  const onThumbMove = (e: React.PointerEvent<HTMLDivElement>) => {
    const el = box.current;
    if (!drag.current || !el) return;
    const track = el.clientHeight - thumb.h - 4;
    if (track > 0) el.scrollTop = drag.current.top + ((e.clientY - drag.current.y) * (el.scrollHeight - el.clientHeight)) / track;
  };
  const onThumbUp = () => {
    drag.current = null;
    flash();
  };

  return (
    <div className="scroller" onMouseMove={flash}>
      <div
        ref={box}
        className={"scroll " + className}
        onScroll={() => {
          update();
          flash();
          // the floating navigation pill hides while scrolling down and returns on scrolling up (or at the top)
          const top = box.current?.scrollTop ?? 0;
          const d = top - lastTop.current;
          if (top < 40) {
            useApp.getState().setNavHidden(false);
            lastTop.current = top;
          } else if (Math.abs(d) > 4) {
            useApp.getState().setNavHidden(d > 0);
            lastTop.current = top; // only after a real move, so slow scrolling still adds up
          }
        }}
      >
        <div ref={inner}>{children}</div>
      </div>
      {thumb.h > 0 && (
        <div className={"sbar" + (show ? " show" : "")} aria-hidden="true">
          <div className="sthumb" style={{ height: thumb.h, transform: `translateY(${thumb.top + 2}px)` }} onPointerDown={onThumbDown} onPointerMove={onThumbMove} onPointerUp={onThumbUp} onPointerCancel={onThumbUp} />
        </div>
      )}
    </div>
  );
}
