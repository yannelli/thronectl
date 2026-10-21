import { createConnection, createServer, type Server, type Socket } from "node:net";
import { chmodSync, existsSync, unlinkSync } from "node:fs";
import { paths } from "./paths.js";

export type ControlHandler = (args: Record<string, unknown>, sock: Socket) => Promise<unknown> | unknown;

interface ControlRequest {
  id: number;
  cmd: string;
  args?: Record<string, unknown>;
}

/** Local JSON-lines control socket used by the CLI (pair, devices, status, notify, say). */
export function startControlServer(handlers: Record<string, ControlHandler>, onStream: Record<string, (sock: Socket) => void>): Server {
  const file = paths.socket();
  if (existsSync(file)) {
    try { unlinkSync(file); } catch { /* in use by another daemon: listen fails below */ }
  }
  const server = createServer((sock) => {
    let buf = "";
    sock.on("data", async (chunk) => {
      buf += chunk.toString("utf8");
      let nl: number;
      while ((nl = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, nl);
        buf = buf.slice(nl + 1);
        let req: ControlRequest;
        try { req = JSON.parse(line) as ControlRequest; } catch { continue; }
        if (onStream[req.cmd]) { onStream[req.cmd](sock); continue; }
        const h = handlers[req.cmd];
        try {
          if (!h) throw new Error(`unknown command ${req.cmd}`);
          const data = await h(req.args ?? {}, sock);
          sock.write(JSON.stringify({ id: req.id, ok: true, data }) + "\n");
        } catch (e) {
          sock.write(JSON.stringify({ id: req.id, ok: false, err: e instanceof Error ? e.message : String(e) }) + "\n");
        }
      }
    });
    sock.on("error", () => { /* client went away */ });
  });
  server.listen(file, () => chmodSync(file, 0o600));
  return server;
}

export function controlConnect(): Promise<Socket> {
  return new Promise((resolve, reject) => {
    const sock = createConnection(paths.socket());
    sock.once("connect", () => resolve(sock));
    sock.once("error", (e) => reject(new Error(`daemon not running (${(e as NodeJS.ErrnoException).code ?? e.message}). Start it with: thronectl daemon`)));
  });
}

export async function controlRequest<T = unknown>(cmd: string, args: Record<string, unknown> = {}): Promise<T> {
  const sock = await controlConnect();
  return new Promise<T>((resolve, reject) => {
    let buf = "";
    sock.on("data", (c) => {
      buf += c.toString("utf8");
      const nl = buf.indexOf("\n");
      if (nl < 0) return;
      const res = JSON.parse(buf.slice(0, nl)) as { ok: boolean; data?: T; err?: string };
      sock.end();
      if (res.ok) resolve(res.data as T);
      else reject(new Error(res.err));
    });
    sock.write(JSON.stringify({ id: 1, cmd, args }) + "\n");
  });
}

/** Opens a stream command and calls onEvent for each JSON line until the socket closes. */
export async function controlStream(cmd: string, onEvent: (ev: Record<string, unknown>, sock: Socket) => void): Promise<Socket> {
  const sock = await controlConnect();
  let buf = "";
  sock.on("data", (c) => {
    buf += c.toString("utf8");
    let nl: number;
    while ((nl = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, nl);
      buf = buf.slice(nl + 1);
      try { onEvent(JSON.parse(line) as Record<string, unknown>, sock); } catch { /* ignore */ }
    }
  });
  sock.write(JSON.stringify({ id: 1, cmd }) + "\n");
  return sock;
}
