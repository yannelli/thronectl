import { execFileSync } from "node:child_process";
import { accessSync, constants } from "node:fs";
import { homedir } from "node:os";
import { delimiter, join } from "node:path";
import { logger } from "./log.js";

const log = logger("env");

const EXTRA_DIRS = [
  "~/.local/bin", "~/.bun/bin", "~/.cargo/bin", "~/.opencode/bin", "~/go/bin",
  "/opt/homebrew/bin", "/opt/homebrew/sbin", "/usr/local/bin", "/home/linuxbrew/.linuxbrew/bin",
];

/** launchd and systemd start us with a minimal PATH, so merge in the login shell's PATH. */
export function adoptLoginShellPath(): void {
  const shell = process.env.SHELL ?? "/bin/zsh";
  let shellPath = "";
  try {
    shellPath = execFileSync(shell, ["-ilc", "printf '__P__%s__P__' \"$PATH\""], {
      encoding: "utf8", timeout: 8000, stdio: ["ignore", "pipe", "ignore"],
    }).split("__P__")[1] ?? "";
  } catch (e) {
    log.debug("login shell PATH lookup failed", e);
  }
  const home = homedir();
  const dirs = [
    ...shellPath.split(delimiter),
    ...(process.env.PATH ?? "").split(delimiter),
    ...EXTRA_DIRS.map((d) => d.replace(/^~/, home)),
  ].filter(Boolean);
  process.env.PATH = [...new Set(dirs)].join(delimiter);
}

export function which(bin: string): string | undefined {
  if (bin.includes("/")) return isExec(bin) ? bin : undefined;
  for (const dir of (process.env.PATH ?? "").split(delimiter)) {
    const full = join(dir, bin);
    if (isExec(full)) return full;
  }
  return undefined;
}

function isExec(p: string): boolean {
  try {
    accessSync(p, constants.X_OK);
    return true;
  } catch {
    return false;
  }
}
