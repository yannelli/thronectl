import { execFile } from "node:child_process";
import { existsSync, readdirSync, readFileSync, statfsSync } from "node:fs";
import { cpus, freemem, loadavg, platform, totalmem, uptime } from "node:os";

export interface Stats {
  cpu: number;
  cores: number[];
  load: number[];
  mem: [number, number];
  swap: [number, number];
  disk: [number, number];
  net: [number, number];
  up: number;
  procs: number;
  batt?: { pct: number; chg: boolean };
  agents: { working: number; idle: number; waiting: number };
}

const MiB = 1024 * 1024;
const GiB = 1024 * MiB;
const r1 = (n: number): number => Math.round(n * 10) / 10;

function sh(cmd: string, args: string[]): Promise<string> {
  return new Promise((resolve) => {
    execFile(cmd, args, { timeout: 4000 }, (err, out) => resolve(err ? "" : out.toString()));
  });
}

type CpuTimes = { idle: number; total: number }[];

function cpuTimes(): CpuTimes {
  return cpus().map((c) => {
    const t = c.times;
    return { idle: t.idle, total: t.user + t.nice + t.sys + t.idle + t.irq };
  });
}

async function memory(): Promise<{ mem: [number, number]; swap: [number, number] }> {
  const total = totalmem();
  if (platform() === "linux" && existsSync("/proc/meminfo")) {
    const info = Object.fromEntries(readFileSync("/proc/meminfo", "utf8").split("\n").map((l) => {
      const m = /^(\w+):\s+(\d+)/.exec(l);
      return m ? [m[1], parseInt(m[2], 10) * 1024] : ["", 0];
    }));
    const avail = info.MemAvailable ?? freemem();
    return {
      mem: [Math.round((total - avail) / MiB), Math.round(total / MiB)],
      swap: [Math.round(((info.SwapTotal ?? 0) - (info.SwapFree ?? 0)) / MiB), Math.round((info.SwapTotal ?? 0) / MiB)],
    };
  }
  if (platform() === "darwin") {
    const [vm, sw] = await Promise.all([sh("vm_stat", []), sh("sysctl", ["-n", "vm.swapusage"])]);
    const page = parseInt(/page size of (\d+)/.exec(vm)?.[1] ?? "16384", 10);
    const pages = (k: string): number => parseInt(new RegExp(`${k}:\\s+(\\d+)`).exec(vm)?.[1] ?? "0", 10);
    const anon = pages("Anonymous pages") - pages("Pages purgeable");
    const used = (anon + pages("Pages wired down") + pages("Pages occupied by compressor")) * page;
    const swapTotal = parseFloat(/total = ([\d.]+)M/.exec(sw)?.[1] ?? "0");
    const swapUsed = parseFloat(/used = ([\d.]+)M/.exec(sw)?.[1] ?? "0");
    return { mem: [Math.round(used / MiB), Math.round(total / MiB)], swap: [Math.round(swapUsed), Math.round(swapTotal)] };
  }
  return { mem: [Math.round((total - freemem()) / MiB), Math.round(total / MiB)], swap: [0, 0] };
}

function disk(): [number, number] {
  const mount = platform() === "darwin" && existsSync("/System/Volumes/Data") ? "/System/Volumes/Data" : "/";
  try {
    const s = statfsSync(mount);
    const total = s.blocks * s.bsize;
    return [r1((total - s.bavail * s.bsize) / GiB), r1(total / GiB)];
  } catch {
    return [0, 0];
  }
}

async function netBytes(): Promise<[number, number]> {
  if (platform() === "linux" && existsSync("/proc/net/dev")) {
    let rx = 0, tx = 0;
    for (const line of readFileSync("/proc/net/dev", "utf8").split("\n").slice(2)) {
      const [name, rest] = line.split(":");
      if (!rest || /^\s*(lo|docker|veth|br-)/.test(name)) continue;
      const f = rest.trim().split(/\s+/).map(Number);
      rx += f[0]; tx += f[8];
    }
    return [rx, tx];
  }
  if (platform() === "darwin") {
    const out = await sh("netstat", ["-ibn"]);
    let rx = 0, tx = 0;
    const seen = new Set<string>();
    for (const line of out.split("\n").slice(1)) {
      const f = line.trim().split(/\s+/);
      if (f.length < 10 || !f[2]?.startsWith("<Link") || f[0].startsWith("lo") || seen.has(f[0])) continue;
      seen.add(f[0]);
      const n = f.length;
      rx += Number(f[n - 5]) || 0; tx += Number(f[n - 2]) || 0;
    }
    return [rx, tx];
  }
  return [0, 0];
}

async function battery(): Promise<Stats["batt"]> {
  if (platform() === "darwin") {
    const out = await sh("pmset", ["-g", "batt"]);
    const m = /(\d+)%;\s*([a-z ]+);/i.exec(out);
    return m ? { pct: parseInt(m[1], 10), chg: /charg|charged|finishing/i.test(m[2]) && !/discharging/i.test(m[2]) } : undefined;
  }
  const dir = "/sys/class/power_supply";
  if (!existsSync(dir)) return undefined;
  const bat = readdirSync(dir).find((d) => d.startsWith("BAT"));
  if (!bat) return undefined;
  const pct = parseInt(readFileSync(`${dir}/${bat}/capacity`, "utf8"), 10);
  const status = readFileSync(`${dir}/${bat}/status`, "utf8").trim();
  return { pct, chg: status === "Charging" || status === "Full" };
}

async function procCount(): Promise<number> {
  if (platform() === "linux") return readdirSync("/proc").filter((d) => /^\d+$/.test(d)).length;
  const out = await sh("ps", ["-axo", "pid="]);
  return out.trim().split("\n").length;
}

/** Samples host stats. Call sample() on an interval; CPU and network are deltas since the last call. */
export class StatsSampler {
  private lastCpu = cpuTimes();
  private lastNet: { at: number; bytes: [number, number] } | undefined;
  latest?: Stats;

  constructor(private readonly agentCounts: () => Stats["agents"]) {}

  async sample(): Promise<Stats> {
    const nowCpu = cpuTimes();
    const cores = nowCpu.map((c, i) => {
      const p = this.lastCpu[i] ?? c;
      const dt = c.total - p.total;
      return dt > 0 ? r1(100 * (1 - (c.idle - p.idle) / dt)) : 0;
    });
    this.lastCpu = nowCpu;
    const [m, bytes, batt, procs] = await Promise.all([memory(), netBytes(), battery().catch(() => undefined), procCount().catch(() => 0)]);
    const now = Date.now();
    let net: [number, number] = [0, 0];
    if (this.lastNet) {
      const dt = (now - this.lastNet.at) / 1000;
      net = [0, 1].map((i) => Math.max(0, Math.round((bytes[i] - this.lastNet!.bytes[i]) / 1024 / dt))) as [number, number];
    }
    this.lastNet = { at: now, bytes };
    const stats: Stats = {
      cpu: r1(cores.reduce((a, b) => a + b, 0) / Math.max(1, cores.length)),
      cores, load: loadavg().map(r1), mem: m.mem, swap: m.swap, disk: disk(), net,
      up: Math.round(uptime()), procs, agents: this.agentCounts(),
    };
    if (batt) stats.batt = batt;
    this.latest = stats;
    return stats;
  }
}
