import { spawn } from "node:child_process";
import { closeSync, existsSync, openSync } from "node:fs";
import { homedir, platform } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { which } from "./env.js";
import { findFirmware, repoRoot, type Firmware } from "./ota.js";
import { configDir } from "./paths.js";

/** Update requests that come from a device: find the newest firmware, or update this host install. */

const CACHE_MS = 10 * 60_000;
let cache: { at: number; fw?: Firmware; note: string } | undefined;

/** The newest firmware image, cached for a while so repeated checks do not hit GitHub each time. */
export async function latestFirmware(force = false): Promise<{ fw?: Firmware; note: string }> {
  if (!force && cache && Date.now() - cache.at < CACHE_MS) return cache;
  const notes: string[] = [];
  const fw = await findFirmware((s) => notes.push(s));
  cache = { at: Date.now(), fw, note: notes.join(" ") };
  return cache;
}

/** True when this install is a git checkout that `thronectl update` can pull. */
export function canUpdateHost(): boolean {
  return !!repoRoot();
}

/** Starts `thronectl update --yes` outside the service's cgroup or process group, since it restarts the service. */
export function startHostUpdate(): string {
  const root = repoRoot();
  if (!root) throw new Error("this install is not a git checkout, so it cannot update itself");
  const cli = join(dirname(fileURLToPath(import.meta.url)), "cli.js");
  const logPath = join(configDir(), "update.log");
  const unit = join(homedir(), ".config", "systemd", "user", "thronectl.service");
  const args = [cli, "update", "--yes"];
  const [cmd, argv] = platform() === "linux" && existsSync(unit) && which("systemd-run")
    ? ["systemd-run", ["--user", "--collect", "--quiet", "--unit", `thronectl-update-${Date.now()}`,
        `--property=StandardOutput=append:${logPath}`, `--property=StandardError=append:${logPath}`, process.execPath, ...args]]
    : [process.execPath, args];
  const out = openSync(logPath, "a");
  try {
    const child = spawn(cmd, argv, { cwd: root, detached: true, stdio: ["ignore", out, out] });
    child.on("error", () => undefined);
    child.unref();
  } finally {
    closeSync(out);
  }
  return logPath;
}
