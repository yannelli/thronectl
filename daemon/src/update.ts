import { execFileSync, spawnSync } from "node:child_process";
import { join, resolve } from "node:path";
import { createInterface } from "node:readline/promises";
import { controlRequest } from "./control.js";
import { which } from "./env.js";
import { compareVersions, findFirmware, loadFirmware, OTA_MIN_FW, repoRoot, type Firmware, type OtaStatus } from "./ota.js";
import { restartService } from "./service.js";

interface Connected { id: string; name: string; fw: string }

const bold = (s: string): string => (process.stdout.isTTY ? `\x1b[1m${s}\x1b[0m` : s);
const dim = (s: string): string => (process.stdout.isTTY ? `\x1b[2m${s}\x1b[0m` : s);
const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

function run(cmd: string, args: string[], cwd: string): void {
  const r = spawnSync(cmd, args, { cwd, stdio: "inherit" });
  if (r.status !== 0) throw new Error(`${cmd} ${args.join(" ")} failed`);
}

async function ask(q: string): Promise<boolean> {
  if (!process.stdin.isTTY) return true;
  const rl = createInterface({ input: process.stdin, output: process.stdout });
  const a = (await rl.question(q)).trim().toLowerCase();
  rl.close();
  return a === "" || a === "y" || a === "yes";
}

async function daemonStatus(waitMs: number): Promise<{ connected: Connected[] } | undefined> {
  const end = Date.now() + waitMs;
  for (;;) {
    try {
      return await controlRequest<{ connected: Connected[] }>("status");
    } catch {
      if (Date.now() > end) return undefined;
      await sleep(500);
    }
  }
}

/** `thronectl update`: pull, rebuild, restart the service, then offer firmware to connected devices. */
export async function cmdUpdate(args: string[]): Promise<void> {
  const root = repoRoot();
  if (!root) throw new Error("this install is not a git checkout. Reinstall with: git clone https://github.com/yannelli/thronectl.git");
  const head = (): string => execFileSync("git", ["-C", root, "rev-parse", "--short", "HEAD"], { encoding: "utf8" }).trim();
  const before = head();
  console.log(bold("Pulling updates..."));
  run("git", ["-C", root, "pull", "--ff-only"], root);
  const after = head();
  const daemonDir = join(root, "daemon");
  if (before === after && !args.includes("--force")) {
    console.log(`Daemon is up to date (${after}).`);
  } else {
    console.log(bold(`Building ${after}...`));
    run("npm", ["install", "--no-audit", "--no-fund"], daemonDir);
    run("npm", ["run", "build"], daemonDir);
    if (!which("throne")) {
      try { run("npm", ["link"], daemonDir); } catch { console.log(dim("Run `npm link` in daemon/ to add the throne alias.")); }
    }
    const restarted = restartService();
    const running = await daemonStatus(0);
    console.log(restarted ?? (running ? "Restart your running `thronectl daemon` to use the new build." : "Start it with: thronectl install (or thronectl daemon)"));
  }
  if (args.includes("--no-firmware")) return;
  // The firmware step runs in the new build, which may speak a newer protocol.
  spawnSync(process.execPath, [join(daemonDir, "dist", "cli.js"), "flash", "--auto", ...args.filter((a) => a === "--yes" || a === "-y")], { stdio: "inherit" });
}

/** `thronectl flash [file] [--device name] [--yes]`: send firmware to connected devices over the link. */
export async function cmdFlash(args: string[]): Promise<void> {
  const auto = args.includes("--auto");
  const yes = args.includes("--yes") || args.includes("-y");
  const di = args.indexOf("--device");
  const match = di >= 0 ? args[di + 1] ?? "" : "";
  const file = args.find((a, i) => !a.startsWith("-") && args[i - 1] !== "--device");
  const status = await daemonStatus(auto ? 15000 : 3000);
  if (!status) throw new Error("the daemon is not running (thronectl install, or thronectl daemon)");
  let fw: Firmware | undefined;
  if (file) fw = loadFirmware(resolve(file), "file");
  else fw = await findFirmware((s) => console.log(dim(s)));
  if (!fw) {
    if (auto) return console.log(dim("No firmware image found (needs gh for releases, or a build in firmware/dist)."));
    throw new Error("no firmware image found. Pass a file: thronectl flash path/to/thronectl-cardputer-adv-vX.Y.Z.bin");
  }
  let targets = status.connected.filter((c) => !match || c.id.startsWith(match) || c.name === match);
  if (!targets.length) {
    const msg = match ? `No connected device matches "${match}".` : "No device online. Connect the Cardputer, then run: thronectl flash";
    return console.log(auto ? dim(msg) : msg);
  }
  if (auto) {
    targets = targets.filter((c) => compareVersions(fw!.version, c.fw) > 0);
    if (!targets.length) return console.log(`Firmware is up to date (v${fw.version}).`);
  }
  for (const c of targets.filter((t) => compareVersions(OTA_MIN_FW, t.fw) > 0)) {
    console.log(`${c.name} runs v${c.fw || "?"}, which cannot update over Wi-Fi. Install ${fw.path} once with Launcher (copy it to the SD card) or esptool.`);
  }
  targets = targets.filter((t) => compareVersions(OTA_MIN_FW, t.fw) <= 0);
  if (!targets.length) return;
  console.log(`Firmware ${bold(`v${fw.version || "?"}`)} from ${fw.source}, ${Math.round(fw.app.length / 1024)} KB`);
  for (const t of targets) {
    if (!yes && !(await ask(`Update ${t.name} (v${t.fw || "?"}) to v${fw.version || "?"}? [Y/n] `))) continue;
    await pushAndWatch(fw, t);
  }
}

async function pushAndWatch(fw: Firmware, t: Connected): Promise<void> {
  await controlRequest("ota.push", { path: fw.path, source: fw.source, device: t.id });
  console.log(`Confirm the update on ${t.name}...`);
  const start = Date.now();
  for (;;) {
    await sleep(300);
    const job = (await controlRequest<OtaStatus[]>("ota.status")).find((j) => j.deviceId === t.id);
    if (!job) throw new Error("the daemon lost the update");
    if (job.state === "sending" && process.stdout.isTTY) {
      const pct = Math.floor((job.sent / job.size) * 100);
      const bar = "#".repeat(Math.round(pct / 4)).padEnd(25, ".");
      process.stdout.write(`\r  [${bar}] ${String(pct).padStart(3)}%  ${Math.round(job.sent / 1024)}/${Math.round(job.size / 1024)} KB`);
    }
    if (job.state === "done") {
      if (process.stdout.isTTY) process.stdout.write("\n");
      if (job.dest === "sd") {
        console.log("Saved to the SD card. Launcher installs apps on this device, so on the Cardputer:");
        console.log("  reboot, press any key on the Launcher screen, open SD, pick the file and choose Install.");
      } else {
        console.log(`${t.name} installed v${fw.version || "?"} and is restarting.`);
      }
      return;
    }
    if (job.state === "failed") {
      if (process.stdout.isTTY) process.stdout.write("\n");
      console.log(`${t.name}: ${job.err}`);
      return;
    }
    if (job.state === "offered" && Date.now() - start > 120_000) {
      console.log("No answer on the device after 2 minutes.");
      return;
    }
  }
}
