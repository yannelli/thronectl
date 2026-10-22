import { execFile, execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { which } from "./env.js";
import { configDir } from "./paths.js";

export const OTA_CHUNK_MAX = 16384;
/** The first firmware that takes updates over the link. Older devices ignore ota.offer. */
export const OTA_MIN_FW = "0.2.0";
const ASSET = /^thronectl-cardputer-adv-v(\d+\.\d+\.\d+)\.bin$/;

export interface Firmware {
  path: string;
  version: string; // from the file name; "" when it has none
  app: Buffer; // what the device writes: merged images are cut at their first app partition
  sha256: string; // hex, of `app`
  source: string;
}

const sha256 = (b: Buffer): string => createHash("sha256").update(b).digest("hex");

/** The app image inside `buf`: a merged flash image (bootloader at 0x0, partition table at 0x8000) or an app image. */
export function appImage(buf: Buffer): Buffer {
  if (buf.length < 0x100 || buf[0] !== 0xe9) throw new Error("not an ESP32 firmware image");
  const merged = buf.length > 0x8020 && buf[0x8000] === 0xaa && buf[0x8001] === 0x50 && buf[0x8002] <= 1;
  if (!merged) return buf;
  for (let off = 0x8000; off + 32 <= Math.min(buf.length, 0x8c00); off += 32) {
    if (buf[off] !== 0xaa || buf[off + 1] !== 0x50) break;
    if (buf[off + 2] !== 0x00) continue;
    const start = buf.readUInt32LE(off + 4);
    if (start >= buf.length || buf[start] !== 0xe9) throw new Error("merged image has no app at its first app partition");
    return buf.subarray(start);
  }
  throw new Error("merged image has no app partition");
}

export function versionOf(name: string): string {
  return /-v(\d+\.\d+\.\d+)/.exec(name)?.[1] ?? "";
}

/** Compares dotted versions: >0 when a is newer. Empty or malformed versions count as oldest. */
export function compareVersions(a: string, b: string): number {
  const pa = a.split(".").map((n) => Number(n) || 0), pb = b.split(".").map((n) => Number(n) || 0);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] ?? 0) - (pb[i] ?? 0);
    if (d) return d;
  }
  return 0;
}

export function loadFirmware(path: string, source: string): Firmware {
  const app = appImage(readFileSync(path));
  return { path, version: versionOf(path), app, sha256: sha256(app), source };
}

/** The git checkout this CLI runs from, if any. */
export function repoRoot(): string | undefined {
  const here = dirname(fileURLToPath(import.meta.url));
  try {
    return execFileSync("git", ["-C", here, "rev-parse", "--show-toplevel"], { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] }).trim();
  } catch {
    return undefined;
  }
}

function githubRepo(root: string): string | undefined {
  try {
    const url = execFileSync("git", ["-C", root, "remote", "get-url", "origin"], { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] }).trim();
    return /github\.com[:/]([^/]+\/[^/]+?)(\.git)?$/.exec(url)?.[1];
  } catch {
    return undefined;
  }
}

const run = promisify(execFile);

/** Downloads the latest release image with the GitHub CLI (works for private repos). */
async function releaseFirmware(root: string, note: (s: string) => void): Promise<Firmware | undefined> {
  const repo = githubRepo(root);
  if (!repo || process.env.THRONECTL_NO_RELEASES) return undefined;
  if (!which("gh")) {
    note("GitHub CLI (gh) not found: cannot fetch release firmware.");
    return undefined;
  }
  let rel: { tagName: string; assets: { name: string }[] };
  try {
    rel = JSON.parse((await run("gh", ["release", "view", "--repo", repo, "--json", "tagName,assets"], { encoding: "utf8" })).stdout);
  } catch (e) {
    note(`gh release view failed: ${(e as Error).message.split("\n")[0]}`);
    return undefined;
  }
  const asset = rel.assets.find((a) => ASSET.test(a.name));
  if (!asset) return undefined;
  const dir = join(configDir(), "firmware");
  mkdirSync(dir, { recursive: true });
  const file = join(dir, asset.name);
  const gh = (pattern: string) => run("gh", ["release", "download", rel.tagName, "--repo", repo, "--pattern", pattern, "--dir", dir, "--clobber"]);
  if (!existsSync(file)) {
    await gh(asset.name);
    if (rel.assets.some((a) => a.name === `${asset.name}.sha256`)) await gh(`${asset.name}.sha256`);
  }
  const sum = `${file}.sha256`;
  if (existsSync(sum) && readFileSync(sum, "utf8").trim().split(/\s+/)[0] !== sha256(readFileSync(file))) {
    rmSync(file);
    throw new Error(`checksum mismatch for ${asset.name}; removed it, try again`);
  }
  return loadFirmware(file, `GitHub release ${rel.tagName}`);
}

/** The newest image built in this checkout (firmware/tools/merge.sh). */
function localFirmware(root: string): Firmware | undefined {
  const dir = join(root, "firmware", "dist");
  if (!existsSync(dir)) return undefined;
  const names = readdirSync(dir).filter((n) => ASSET.test(n)).sort((a, b) => compareVersions(versionOf(b), versionOf(a)));
  return names.length ? loadFirmware(join(dir, names[0]), "local build") : undefined;
}

/** The newest firmware available from the latest GitHub release or a local build. */
export async function findFirmware(note: (s: string) => void): Promise<Firmware | undefined> {
  const root = repoRoot();
  if (!root) return undefined;
  const found = [await releaseFirmware(root, note), localFirmware(root)].filter((f): f is Firmware => !!f);
  return found.sort((a, b) => compareVersions(b.version, a.version))[0];
}

export type OtaState = "offered" | "sending" | "done" | "failed";

export interface OtaStatus {
  deviceId: string;
  name: string;
  version: string;
  size: number;
  sent: number;
  state: OtaState;
  dest: string; // "flash" or "sd" once the device reports it
  err: string;
}

/** Firmware offered to each connected device, and how far each transfer got. */
export class OtaStore {
  private jobs = new Map<string, OtaStatus & { app: Buffer }>();

  offer(deviceId: string, name: string, fw: Firmware): void {
    this.jobs.set(deviceId, { deviceId, name, version: fw.version, size: fw.app.length, sent: 0, state: "offered", dest: "", err: "", app: fw.app });
  }

  read(deviceId: string, off: number, n: number): Buffer {
    const job = this.jobs.get(deviceId);
    if (!job || (job.state !== "offered" && job.state !== "sending")) throw new Error("no firmware offered");
    if (!Number.isInteger(off) || off < 0 || off > job.size) throw new Error("bad offset");
    job.state = "sending";
    const chunk = job.app.subarray(off, Math.min(job.size, off + Math.min(n, OTA_CHUNK_MAX)));
    job.sent = Math.max(job.sent, off + chunk.length);
    return chunk;
  }

  finish(deviceId: string, ok: boolean, err: string, dest: string): void {
    const job = this.jobs.get(deviceId);
    if (!job) return;
    job.state = ok ? "done" : "failed";
    job.err = ok ? "" : err || "failed on the device";
    job.dest = dest;
  }

  disconnected(deviceId: string): void {
    const job = this.jobs.get(deviceId);
    if (job && (job.state === "offered" || job.state === "sending")) this.finish(deviceId, false, "device disconnected", job.dest);
  }

  list(): OtaStatus[] {
    return [...this.jobs.values()].map(({ app: _app, ...s }) => s);
  }
}
