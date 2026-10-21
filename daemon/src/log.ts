type Level = "debug" | "info" | "warn" | "error";

const order: Record<Level, number> = { debug: 10, info: 20, warn: 30, error: 40 };
let threshold: number = order[(process.env.THRONECTL_LOG as Level) ?? "info"] ?? order.info;

export function setLogLevel(level: Level): void {
  threshold = order[level];
}

function emit(level: Level, scope: string, msg: string, extra?: unknown): void {
  if (order[level] < threshold) return;
  const ts = new Date().toISOString().slice(11, 19);
  const tail = extra === undefined ? "" : " " + (extra instanceof Error ? extra.message : JSON.stringify(extra));
  const line = `${ts} ${level.padEnd(5)} [${scope}] ${msg}${tail}`;
  (level === "error" || level === "warn" ? process.stderr : process.stdout).write(line + "\n");
}

export interface Logger {
  debug(msg: string, extra?: unknown): void;
  info(msg: string, extra?: unknown): void;
  warn(msg: string, extra?: unknown): void;
  error(msg: string, extra?: unknown): void;
}

export function logger(scope: string): Logger {
  return {
    debug: (m, e) => emit("debug", scope, m, e),
    info: (m, e) => emit("info", scope, m, e),
    warn: (m, e) => emit("warn", scope, m, e),
    error: (m, e) => emit("error", scope, m, e),
  };
}
