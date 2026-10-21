import { chmodSync, existsSync, statSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { which } from "./env.js";
import { logger } from "./log.js";

const log = logger("pty");

/** node-pty's install folder. */
export function ptyRoot(): string {
  return dirname(createRequire(import.meta.url).resolve("node-pty/package.json"));
}

/** Every spawn-helper node-pty could use on this platform. macOS spawns through it; Linux does not. */
export function spawnHelpers(root = ptyRoot()): string[] {
  const dirs = [join(root, "prebuilds", `${process.platform}-${process.arch}`), join(root, "build", "Release")];
  return dirs.map((d) => join(d, "spawn-helper")).filter((f) => existsSync(f));
}

const executable = (file: string): boolean => (statSync(file).mode & 0o111) === 0o111;

/** node-pty 1.1 installs spawn-helper without the execute bit, so macOS spawns fail with "posix_spawnp failed". */
export function fixSpawnHelper(root = ptyRoot()): string[] {
  const fixed: string[] = [];
  for (const f of spawnHelpers(root)) {
    if (executable(f)) continue;
    chmodSync(f, statSync(f).mode | 0o755);
    fixed.push(f);
  }
  return fixed;
}

let checked = false;

/** Runs fixSpawnHelper once per process. Logs instead of throwing, so a read-only install still starts. */
export function ensureSpawnHelper(): void {
  if (checked) return;
  checked = true;
  try {
    for (const f of fixSpawnHelper()) log.info(`made ${f} executable (node-pty installs it without the execute bit)`);
  } catch (e) {
    log.warn(`cannot fix node-pty spawn-helper: ${(e as Error).message}`);
  }
}

/** On macOS a missing command or folder makes the helper exit 1 silently, so check both before spawning. */
export function checkSpawn(cmd: string, cwd: string): void {
  if (!existsSync(cwd)) throw new Error(`folder not found: ${cwd}`);
  if (!which(cmd)) throw new Error(`${cmd} not found on the daemon's PATH. Run \`thronectl doctor\`, or set an absolute path.`);
}

/** Turns a node-pty spawn failure into a message that says what to fix. */
export function explainSpawnError(err: unknown, cmd: string): Error {
  const msg = err instanceof Error ? err.message : String(err);
  if (/posix_spawn/.test(msg)) {
    let stuck: string[] = [];
    try { stuck = spawnHelpers().filter((f) => !executable(f)); } catch { /* no node-pty */ }
    if (stuck.length) return new Error(`node-pty helper is not executable. Run: chmod +x ${stuck.join(" ")}`);
  }
  return new Error(`could not start ${cmd}: ${msg}`);
}
