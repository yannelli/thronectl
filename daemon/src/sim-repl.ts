import { createInterface } from "node:readline/promises";
import { formatCode } from "./pairing.js";
import { SimDevice } from "./sim.js";

const HELP = `commands: stats | agents | harness | projects | procs | new <harness> <cwd> <prompt...> |
  send <id> <text> | tail <id> | last <id> | stop <id> | kill <id> | permit <id> y|n |
  shell | agent-term <id> | say <text> | quit`;

/** Interactive simulated device for trying the daemon without hardware. */
export async function runSim(host: string, port: number): Promise<void> {
  const dev = SimDevice.load();
  let screen: string[] = [];
  let termId = -1;
  dev.onAuth = (state, m) => {
    if (state === "pair") console.log(`Pairing code: ${formatCode(String(m.code))}. Approve it with: thronectl pair`);
  };
  dev.onEvent = (e, d) => {
    const data = d as Record<string, unknown>;
    if (e === "notify") console.log(`\n[notify:${String(data.kind)}] ${String(data.title)}: ${String(data.body)}`);
    if (e === "term" && data.tid === termId) {
      const rows = data.rows as [number, [number, number, number, string][]][];
      if (data.full) screen = [];
      for (const [y, spans] of rows) screen[y] = spans.map((s) => s[3]).join("");
    }
    if (e === "term_exit") console.log(`\n[terminal ${String(data.tid)} exited ${String(data.code)}]`);
  };
  const h = await dev.connect(host, port);
  console.log(`Connected to ${h.name} (${h.id}). ${HELP}`);
  await dev.call("sub", { topics: ["agents", "notify"] });
  const rl = createInterface({ input: process.stdin, output: process.stdout });
  const show = (v: unknown): void => console.log(JSON.stringify(v, null, 1));
  for (;;) {
    const line = (await rl.question("sim> ")).trim();
    const [cmd, a1, ...rest] = line.split(/\s+/);
    try {
      switch (cmd) {
        case "": break;
        case "stats": show(await dev.call("stats.get")); break;
        case "agents": show(await dev.call("agents.list")); break;
        case "harness": show(await dev.call("harness.list")); break;
        case "projects": show(await dev.call("projects.list")); break;
        case "procs": show(await dev.call("procs.list", { n: 10 })); break;
        case "new": show(await dev.call("agent.create", { harness: a1, cwd: rest[0], prompt: rest.slice(1).join(" ") })); break;
        case "send": show(await dev.call("agent.send", { id: a1, text: rest.join(" ") })); break;
        case "tail": console.log(((await dev.call<{ lines: string[] }>("agent.tail", { id: a1, n: 30 })).lines).join("\n")); break;
        case "last": console.log((await dev.call<{ text: string }>("agent.last", { id: a1 })).text); break;
        case "stop": show(await dev.call("agent.stop", { id: a1 })); break;
        case "kill": show(await dev.call("agent.kill", { id: a1 })); break;
        case "permit": show(await dev.call("agent.permit", { id: a1, allow: rest[0] === "y" })); break;
        case "say": show(await dev.call("tts.say", { text: [a1, ...rest].join(" ") })); break;
        case "shell": case "agent-term": {
          const p = cmd === "shell" ? { shell: true, cols: 60, rows: 20 } : { agent: a1, cols: 60, rows: 20 };
          termId = (await dev.call<{ tid: number }>("term.open", p)).tid;
          console.log("terminal open: type lines to send, '.' to print the screen, '.q' to close");
          for (;;) {
            const l = await rl.question("term> ");
            if (l === ".q") { await dev.call("term.close", { tid: termId }); break; }
            if (l === ".") { console.log(screen.map((s) => s ?? "").join("\n")); continue; }
            await dev.call("term.input", { tid: termId, d: l + "\r" });
            await new Promise((r) => setTimeout(r, 400));
            console.log(screen.map((s) => s ?? "").join("\n"));
          }
          break;
        }
        case "quit": case "exit": dev.close(); rl.close(); return;
        default: console.log(HELP);
      }
    } catch (e) {
      console.log(`error: ${(e as Error).message}`);
    }
  }
}
