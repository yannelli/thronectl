import { execFile } from "node:child_process";
import { basename } from "node:path";

export interface Proc {
  pid: number;
  name: string;
  cpu: number;
  mem: number;
  user: string;
}

export function listProcs(sort: "cpu" | "mem", n: number): Promise<Proc[]> {
  return new Promise((resolve, reject) => {
    execFile("ps", ["-axo", "pid=,pcpu=,pmem=,user=,comm="], { timeout: 5000, maxBuffer: 8 << 20 }, (err, out) => {
      if (err) return reject(err);
      const procs: Proc[] = [];
      for (const line of out.toString().split("\n")) {
        const m = /^\s*(\d+)\s+([\d.]+)\s+([\d.]+)\s+(\S+)\s+(.+)$/.exec(line);
        if (!m) continue;
        procs.push({ pid: +m[1], cpu: +m[2], mem: +m[3], user: m[4], name: basename(m[5].trim()).slice(0, 32) });
      }
      procs.sort((a, b) => (sort === "mem" ? b.mem - a.mem : b.cpu - a.cpu));
      resolve(procs.slice(0, Math.min(Math.max(n, 1), 60)));
    });
  });
}

export function killProc(pid: number, sig: string): void {
  if (!Number.isInteger(pid) || pid <= 1 || pid === process.pid) throw new Error("refusing to signal that process");
  process.kill(pid, sig === "KILL" ? "SIGKILL" : "SIGTERM");
}
