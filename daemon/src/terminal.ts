import { EventEmitter } from "node:events";
import xterm from "@xterm/headless";
import pty from "node-pty";
import { checkSpawn, ensureSpawnHelper, explainSpawnError } from "./spawn.js";

type XTerminal = InstanceType<typeof xterm.Terminal>;
type IPty = ReturnType<typeof pty.spawn>;

export type Span = [fg: number, bg: number, flags: number, text: string];
export type Row = [y: number, spans: Span[]];

export interface TermUpdate {
  full: boolean;
  cx: number;
  cy: number;
  cv: boolean;
  rows: Row[];
}

const ANSI16 = [
  "#000000", "#CD3131", "#0DBC79", "#E5E510", "#2472C8", "#BC3FBC", "#11A8CD", "#E5E5E5",
  "#666666", "#F14C4C", "#23D18B", "#F5F543", "#3B8EEA", "#D670D6", "#29B8DB", "#FFFFFF",
];

const to565 = (r: number, g: number, b: number): number => ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);

const PALETTE: number[] = (() => {
  const out: number[] = ANSI16.map((h) => {
    const v = parseInt(h.slice(1), 16);
    return to565(v >> 16, (v >> 8) & 0xff, v & 0xff);
  });
  const lv = [0, 95, 135, 175, 215, 255];
  for (let r = 0; r < 6; r++) for (let g = 0; g < 6; g++) for (let b = 0; b < 6; b++) out.push(to565(lv[r], lv[g], lv[b]));
  for (let i = 0; i < 24; i++) { const v = 8 + 10 * i; out.push(to565(v, v, v)); }
  return out;
})();

/** A PTY plus a headless terminal emulator that tracks its screen. */
export class PtyTerm extends EventEmitter {
  readonly term: XTerminal;
  private proc: IPty;
  cursorVisible = true;
  exited = false;
  exitCode = 0;
  lastOutput = Date.now();

  constructor(cmd: string, args: string[], opts: { cwd: string; cols: number; rows: number; env?: Record<string, string> }) {
    super();
    this.setMaxListeners(50);
    this.term = new xterm.Terminal({ cols: opts.cols, rows: opts.rows, scrollback: 2000, allowProposedApi: true });
    const env = { ...process.env, TERM: "xterm-256color", COLORTERM: "truecolor", ...opts.env } as Record<string, string>;
    ensureSpawnHelper();
    checkSpawn(cmd, opts.cwd);
    try {
      this.proc = pty.spawn(cmd, args, { name: "xterm-256color", cols: opts.cols, rows: opts.rows, cwd: opts.cwd, env });
    } catch (e) {
      throw explainSpawnError(e, cmd);
    }
    this.proc.onData((d) => {
      this.lastOutput = Date.now();
      const show = d.lastIndexOf("\x1b[?25h"), hide = d.lastIndexOf("\x1b[?25l");
      if (show !== hide) this.cursorVisible = show > hide;
      this.term.write(d, () => this.emit("render"));
      this.emit("data", d);
    });
    this.proc.onExit(({ exitCode }) => {
      this.exited = true;
      this.exitCode = exitCode;
      this.emit("exit", exitCode);
    });
    // Answer cursor position and device queries the way a real terminal would.
    this.term.onData((d) => { if (!this.exited) this.proc.write(d); });
  }

  get pid(): number {
    return this.proc.pid;
  }

  write(data: string): void {
    if (!this.exited) this.proc.write(data);
  }

  resize(cols: number, rows: number): void {
    if (this.exited || (cols === this.term.cols && rows === this.term.rows)) return;
    this.proc.resize(cols, rows);
    this.term.resize(cols, rows);
    this.emit("render");
  }

  kill(): void {
    if (!this.exited) {
      try { this.proc.kill(); } catch { /* already gone */ }
    }
  }

  /** Plain text of the visible screen, one string per row. */
  screen(): string[] {
    const b = this.term.buffer.active;
    const out: string[] = [];
    for (let y = 0; y < this.term.rows; y++) out.push(b.getLine(b.viewportY + y)?.translateToString(true) ?? "");
    return out;
  }

  /** Last n non-empty lines from scrollback and screen. */
  tail(n: number): string[] {
    const b = this.term.buffer.active;
    const out: string[] = [];
    for (let y = b.length - 1; y >= 0 && out.length < n; y--) {
      const s = b.getLine(y)?.translateToString(true) ?? "";
      if (out.length === 0 && s.trim() === "") continue;
      out.push(s);
    }
    return out.reverse();
  }

  spansFor(y: number): Span[] {
    const b = this.term.buffer.active;
    const line = b.getLine(b.viewportY + y);
    const spans: Span[] = [];
    if (!line) return spans;
    const cell = b.getNullCell();
    for (let x = 0; x < this.term.cols; x++) {
      line.getCell(x, cell);
      const w = cell.getWidth();
      if (w === 0) continue;
      let ch = cell.getChars() || " ";
      if (w === 2) ch += " ";
      const fg = cell.isFgDefault() ? -1 : cell.isFgPalette() ? PALETTE[cell.getFgColor()] ?? -1 : rgbTo565(cell.getFgColor());
      const bg = cell.isBgDefault() ? -1 : cell.isBgPalette() ? PALETTE[cell.getBgColor()] ?? -1 : rgbTo565(cell.getBgColor());
      const fl = (cell.isBold() ? 1 : 0) | (cell.isUnderline() ? 2 : 0) | (cell.isItalic() ? 4 : 0) | (cell.isInverse() ? 8 : 0) | (cell.isDim() ? 16 : 0);
      const last = spans[spans.length - 1];
      if (last && last[0] === fg && last[1] === bg && last[2] === fl) last[3] += ch;
      else spans.push([fg, bg, fl, ch]);
    }
    const tail = spans[spans.length - 1];
    if (tail && tail[1] === -1 && (tail[2] & 0b1010) === 0) {
      tail[3] = tail[3].trimEnd();
      if (tail[3] === "") spans.pop();
    }
    return spans;
  }
}

function rgbTo565(v: number): number {
  return to565((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

/** Streams screen diffs of a PtyTerm to one device at a capped frame rate. */
export class TermView {
  private cache: string[] = [];
  private dirty = true;
  private forceFull = true;
  private timer: NodeJS.Timeout;
  private readonly onRender = (): void => { this.dirty = true; };
  private readonly onExit = (code: number): void => this.exitCb(code);

  constructor(
    readonly src: PtyTerm,
    private readonly send: (u: TermUpdate) => boolean,
    private readonly exitCb: (code: number) => void,
    private readonly ownsSource: boolean,
  ) {
    src.on("render", this.onRender);
    src.on("exit", this.onExit);
    this.timer = setInterval(() => this.flush(), 66);
    if (src.exited) queueMicrotask(() => exitCb(src.exitCode));
  }

  private lastCursor = "";

  flush(): void {
    const b = this.src.term.buffer.active;
    const cursorKey = `${b.cursorX},${b.cursorY},${this.src.cursorVisible}`;
    if (!this.dirty && cursorKey === this.lastCursor) return;
    const full = this.forceFull || this.cache.length !== this.src.term.rows;
    const rows: Row[] = [];
    const next: string[] = [];
    for (let y = 0; y < this.src.term.rows; y++) {
      const spans = this.src.spansFor(y);
      const key = JSON.stringify(spans);
      next.push(key);
      if (full || this.cache[y] !== key) rows.push([y, spans]);
    }
    if (!this.send({ full, cx: b.cursorX, cy: b.cursorY, cv: this.src.cursorVisible, rows })) return;
    this.cache = next;
    this.dirty = false;
    this.forceFull = false;
    this.lastCursor = cursorKey;
  }

  refresh(): void {
    this.forceFull = true;
    this.dirty = true;
  }

  close(): void {
    clearInterval(this.timer);
    this.src.off("render", this.onRender);
    this.src.off("exit", this.onExit);
    if (this.ownsSource) this.src.kill();
  }
}
