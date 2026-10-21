import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdirSync, mkdtempSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { checkSpawn, explainSpawnError, fixSpawnHelper } from "../src/spawn.js";
import { PtyTerm } from "../src/terminal.js";

test("fixSpawnHelper sets the execute bit that node-pty 1.1 prebuilds lack", () => {
  const root = mkdtempSync(join(tmpdir(), "pty-"));
  const dir = join(root, "prebuilds", `${process.platform}-${process.arch}`);
  mkdirSync(dir, { recursive: true });
  const helper = join(dir, "spawn-helper");
  writeFileSync(helper, "#!/bin/sh\n", { mode: 0o644 });
  assert.deepEqual(fixSpawnHelper(root), [helper]);
  assert.equal(statSync(helper).mode & 0o777, 0o755);
  assert.deepEqual(fixSpawnHelper(root), []);
});

test("spawn checks name the missing folder or command", () => {
  assert.throws(() => checkSpawn("/bin/sh", "/no/such/dir"), /folder not found/);
  assert.throws(() => checkSpawn("no-such-cli-xyz", tmpdir()), /not found on the daemon's PATH/);
  assert.doesNotThrow(() => checkSpawn("/bin/sh", tmpdir()));
  assert.match(explainSpawnError(new Error("boom"), "/bin/sh").message, /could not start \/bin\/sh: boom/);
});

test("PtyTerm refuses a command that is not on PATH", () => {
  assert.throws(() => new PtyTerm("no-such-cli-xyz", [], { cwd: tmpdir(), cols: 40, rows: 10 }), /not found on the daemon's PATH/);
});
