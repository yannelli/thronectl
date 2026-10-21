#!/usr/bin/env node
import { createInterface } from "node:readline/promises";
import { existsSync, readFileSync } from "node:fs";
import { networkInterfaces, platform } from "node:os";
import { spawn } from "node:child_process";
import { loadConfig, loadEnvFile, saveConfig, setConfigValue } from "./config.js";
import { controlRequest, controlStream } from "./control.js";
import { probe } from "./discovery.js";
import { adoptLoginShellPath, which } from "./env.js";
import { resolveHarnesses } from "./harness.js";
import { loadIdentity } from "./identity.js";
import { setLogLevel } from "./log.js";
import { formatCode, type PairRequest } from "./pairing.js";
import { configDir, paths, VERSION } from "./paths.js";
import { Daemon } from "./server.js";
import { installService, uninstallService } from "./service.js";
import { runSim } from "./sim-repl.js";
import { fixSpawnHelper, spawnHelpers } from "./spawn.js";
import { cmdFlash, cmdUpdate } from "./update.js";
import { describeStt } from "./voice/stt.js";
import { describeTts } from "./voice/tts.js";

const HELP = `thronectl ${VERSION}: command your coding agents from a Cardputer

Usage: thronectl <command>   (or the short alias: throne <command>)

  daemon             Run the host daemon in the foreground
  install            Run the daemon at login (launchd on macOS, systemd on Linux)
  uninstall          Remove the login service
  update [--no-firmware] [--yes]   Pull, rebuild and restart the daemon, then offer new firmware to devices
  flash [file] [--device <name>] [--yes]   Send firmware to connected devices (default: latest release)
  pair               Approve or deny devices that want to pair (y/n)
  devices            List paired devices
  devices rm <id>    Forget a paired device (ID prefix or name)
  status             Show daemon status and connected devices
  doctor             Check agent CLIs, Paseo, voice providers and network
  config [path|get <key>|set <key> <value>]
  notify <title> [body] [--kind done|attn|error|info]   Show a toast on devices
  say <text>         Speak text on connected devices
  scan               Find daemons on the LAN
  sim [--host H] [--port P]   Run a simulated device (for testing)
  logs               Follow the daemon log (service mode)
`;

const bold = (s: string): string => (process.stdout.isTTY ? `\x1b[1m${s}\x1b[0m` : s);
const dim = (s: string): string => (process.stdout.isTTY ? `\x1b[2m${s}\x1b[0m` : s);
const green = (s: string): string => (process.stdout.isTTY ? `\x1b[32m${s}\x1b[0m` : s);

function flag(args: string[], name: string): string | undefined {
  const i = args.indexOf(name);
  if (i < 0) return undefined;
  const v = args[i + 1];
  args.splice(i, 2);
  return v;
}

/** Asks y/n for each pairing request, one at a time. */
function pairPrompter(answer: (req: PairRequest, yes: boolean) => Promise<void>): (req: PairRequest) => void {
  const queue: PairRequest[] = [];
  let busy = false;
  const next = async (): Promise<void> => {
    if (busy) return;
    const req = queue.shift();
    if (!req) return;
    busy = true;
    const rl = createInterface({ input: process.stdin, output: process.stdout });
    process.stdout.write(`\n${bold("Pairing request")} from ${bold(req.deviceName)} ${dim(`(${req.addr}, fw ${req.fw || "?"}, id ${req.deviceId})`)}\n`);
    process.stdout.write(`  Code on the device: ${bold(formatCode(req.code))}\n`);
    const reply = (await rl.question("  Does the device show this code? Pair it? [y/N] ")).trim().toLowerCase();
    rl.close();
    const yes = reply === "y" || reply === "yes";
    await answer(req, yes).catch((e: Error) => console.error(`  ${e.message}`));
    console.log(yes ? green("  Paired. The device will remember this host.") : "  Denied.");
    busy = false;
    void next();
  };
  return (req) => { queue.push(req); void next(); };
}

async function cmdDaemon(args: string[]): Promise<void> {
  if (args.includes("--verbose") || args.includes("-v")) setLogLevel("debug");
  loadEnvFile();
  adoptLoginShellPath();
  const d = new Daemon();
  await d.start();
  if (process.stdin.isTTY) {
    const ask = pairPrompter(async (req, yes) => { d.broker.answer(req.id, yes); });
    d.broker.on("request", ask);
    console.log(dim(`Pairing requests are asked here. From another terminal: thronectl pair`));
  }
  const stop = (): void => { d.stop(); process.exit(0); };
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);
}

async function cmdPair(): Promise<void> {
  const ask = pairPrompter(async (req, yes) => { await controlRequest("pair.answer", { id: req.id, yes }); });
  console.log(`Waiting for pairing requests. On the Cardputer press ${bold("h")} (Hosts) and pick this computer. Ctrl-C to stop.`);
  const sock = await controlStream("pair.watch", (ev) => {
    if (ev.ev === "pair.request") ask(ev.req as PairRequest);
    if (ev.ev === "pair.resolved" && ev.reason === "timeout") console.log(dim(`\n  Request ${String(ev.id)} expired.`));
  });
  sock.on("close", () => { console.log("Daemon closed the connection."); process.exit(1); });
}

async function cmdDevices(args: string[]): Promise<void> {
  if (args[0] === "rm" || args[0] === "remove") {
    if (!args[1]) throw new Error("usage: thronectl devices rm <id|name>");
    const removed = await controlRequest<{ name: string; id: string }[]>("devices.rm", { match: args[1] });
    console.log(removed.length ? removed.map((d) => `Removed ${d.name} (${d.id})`).join("\n") : "No matching device.");
    return;
  }
  const list = await controlRequest<{ id: string; name: string; online: boolean; lastSeen?: string; lastAddr?: string; fw?: string }[]>("devices.list");
  if (!list.length) return console.log("No paired devices. Run: thronectl pair");
  for (const d of list) {
    console.log(`${d.online ? green("●") : dim("○")} ${bold(d.name)}  ${dim(d.id)}  ${d.lastAddr ?? ""}  fw ${d.fw ?? "?"}  ${dim(d.lastSeen ? `seen ${d.lastSeen}` : "")}`);
  }
}

async function cmdStatus(): Promise<void> {
  const s = await controlRequest<{
    host: { name: string; id: string; os: string; paseo: boolean }; port: number;
    connected: { name: string; addr: string; fw: string; since: number; stats?: number }[];
    agents: { working: number; idle: number; waiting: number }; stt: string; tts: string; pending: PairRequest[];
  }>("status");
  console.log(`${green("●")} ${bold(s.host.name)} ${dim(`id ${s.host.id}`)} on port ${s.port}`);
  console.log(`  agents: ${s.agents.working} working, ${s.agents.waiting} waiting, ${s.agents.idle} idle   paseo: ${s.host.paseo ? "yes" : "no"}`);
  console.log(`  speech-to-text: ${s.stt}\n  text-to-speech: ${s.tts}`);
  if (!s.connected.length) console.log("  devices online: none");
  for (const c of s.connected) {
    const mins = Math.round((Date.now() - c.since) / 60_000);
    const stats = c.stats ? `stats every ${c.stats / 1000} s` : "stats paused";
    console.log(`  device: ${bold(c.name)} ${dim(`${c.addr}  fw ${c.fw || "?"}  ${stats}  online ${mins} min`)}`);
  }
  if (s.pending.length) console.log(`  ${bold(`${s.pending.length} pairing request(s) waiting`)}: run thronectl pair`);
}

/** Checks node-pty's spawn-helper, which must be executable on macOS. Fixes it when it can. */
function ptyStatus(): string {
  try {
    const fixed = fixSpawnHelper();
    if (fixed.length) return green(`fixed: made ${fixed.join(", ")} executable`);
    const helpers = spawnHelpers();
    return green(helpers.length ? `ok (${helpers.length} spawn-helper)` : "ok");
  } catch (e) {
    return `node-pty problem: ${(e as Error).message}`;
  }
}

async function cmdDoctor(): Promise<void> {
  loadEnvFile();
  adoptLoginShellPath();
  const cfg = loadConfig();
  const id = loadIdentity();
  console.log(`${bold("thronectl")} ${VERSION}  node ${process.version}  ${platform()}`);
  console.log(`  config: ${configDir()}  host id: ${id.id}  port: ${cfg.port}`);
  const ips = Object.values(networkInterfaces()).flat().filter((i) => i && i.family === "IPv4" && !i.internal).map((i) => i!.address);
  console.log(`  LAN addresses: ${ips.join(", ") || "none"}`);
  console.log(bold("\nAgent CLIs (direct):"));
  for (const h of resolveHarnesses(cfg.harness)) console.log(`  ${h.path ? green("✓") : dim("✗")} ${h.name.padEnd(14)} ${dim(h.path ?? h.bins.join(" | "))}`);
  const paseo = which(cfg.paseo.bin);
  console.log(`\n${bold("Paseo:")} ${paseo ? green(paseo) : dim("not installed")}`);
  console.log(`${bold("Speech-to-text:")} ${describeStt(cfg)}`);
  console.log(`${bold("Text-to-speech:")} ${describeTts(cfg)}`);
  console.log(`${bold("Shell access:")} ${cfg.allowShell ? `enabled (${cfg.shell})` : "disabled"}`);
  console.log(`${bold("Terminals:")} ${ptyStatus()}`);
  const running = await controlRequest("status").then(() => true, () => false);
  console.log(`${bold("Daemon:")} ${running ? green("running") : "not running (thronectl daemon or thronectl install)"}`);
}

function cmdConfig(args: string[]): void {
  const cfg = loadConfig();
  if (args[0] === "path") return console.log(paths.config());
  if (args[0] === "get") {
    const v = (args[1] ?? "").split(".").filter(Boolean).reduce<unknown>((o, k) => (o as Record<string, unknown>)?.[k], cfg);
    return console.log(JSON.stringify(v, null, 2));
  }
  if (args[0] === "set") {
    if (!args[1] || args[2] === undefined) throw new Error("usage: thronectl config set <key> <value>");
    saveConfig(setConfigValue(cfg, args[1], args.slice(2).join(" ")));
    void controlRequest("reload").catch(() => {});
    return console.log(`set ${args[1]}`);
  }
  console.log(JSON.stringify(cfg, null, 2));
}

async function main(): Promise<void> {
  const [cmd, ...args] = process.argv.slice(2);
  switch (cmd) {
    case "daemon": case "up": return cmdDaemon(args);
    case "pair": return cmdPair();
    case "devices": return cmdDevices(args);
    case "status": return cmdStatus();
    case "doctor": return cmdDoctor();
    case "config": return cmdConfig(args);
    case "install": adoptLoginShellPath(); return console.log(installService());
    case "uninstall": return console.log(uninstallService());
    case "update": case "upgrade": return cmdUpdate(args);
    case "flash": return cmdFlash(args);
    case "notify": {
      const kind = flag(args, "--kind") ?? "info";
      const r = await controlRequest<{ sent: number }>("notify", { title: args[0] ?? "thronectl", body: args.slice(1).join(" "), kind });
      return console.log(`sent to ${r.sent} device(s)`);
    }
    case "say": {
      const r = await controlRequest<{ sent: number }>("say", { text: args.join(" ") });
      return console.log(`speaking on ${r.sent} device(s)`);
    }
    case "scan": {
      const found = await probe(loadConfig().port);
      if (!found.length) return console.log("No daemons answered.");
      for (const b of found) console.log(`${bold(b.name)}  id ${b.id}  port ${b.port}  ${b.os}`);
      return;
    }
    case "sim": return runSim(flag(args, "--host") ?? "127.0.0.1", Number(flag(args, "--port") ?? loadConfig().port));
    case "logs": {
      if (platform() === "linux" && !existsSync(paths.log())) { spawn("journalctl", ["--user", "-u", "thronectl", "-f"], { stdio: "inherit" }); return; }
      if (!existsSync(paths.log())) throw new Error(`no log file at ${paths.log()}`);
      process.stdout.write(readFileSync(paths.log(), "utf8").split("\n").slice(-50).join("\n"));
      spawn("tail", ["-f", "-n", "0", paths.log()], { stdio: "inherit" });
      return;
    }
    case "version": case "--version": case "-v": return console.log(VERSION);
    case undefined: case "help": case "--help": case "-h": return console.log(HELP);
    default: throw new Error(`unknown command: ${cmd}\n\n${HELP}`);
  }
}

main().catch((e: Error) => {
  console.error(`thronectl: ${e.message}`);
  process.exit(1);
});
