#!/usr/bin/env bash
# Builds the firmware link code for the host and runs it against a throwaway daemon.
# Needs mbedTLS 2.28 headers and libmbedcrypto: set MBEDTLS_ROOT if they are not in /usr.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../.."
repo="$fw/.."
libs="$fw/.pio/libdeps/cardputer-adv"
mb="${MBEDTLS_ROOT:-/usr}"
mblib="$(dirname "$(find "$mb" -name libmbedcrypto.a | head -1)")"
work="$(mktemp -d)"
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$work"' EXIT

g++ -std=c++17 -O1 -g -DLGFX_LINUX_FB -I"$fw/src" -I"$libs/ArduinoJson/src" -I"$libs/M5GFX/src" -I"$mb/include" \
  "$here/linkcheck.cpp" "$fw/src/net/link.cpp" "$fw/src/net/crypto.cpp" "$fw/src/core/settings.cpp" "$fw/src/core/led.cpp" "$fw/src/core/updater.cpp" \
  "$mblib/libmbedcrypto.a" -o "$work/linkcheck"

(cd "$repo/daemon" && { [ -d node_modules ] || npm install --silent; } && npm run -s build)
port=$((20000 + RANDOM % 20000))
export THRONECTL_HOME="$work/home"
mkdir -p "$THRONECTL_HOME"
cat > "$THRONECTL_HOME/config.json" <<EOF
{"port": $port, "bind": "127.0.0.1", "discovery": false, "paseo": {"enabled": false},
 "harness": {"echo": {"bin": "/bin/sh", "args": ["-c", "echo READY \$0; while read l; do echo got \$l; done"]}}}
EOF
node "$repo/daemon/dist/cli.js" daemon > "$work/daemon.log" 2>&1 &
for _ in $(seq 50); do [ -S "$THRONECTL_HOME/control.sock" ] && break; sleep 0.1; done

# Approves the first pairing request, like answering y in `thronectl pair`.
node --input-type=module -e "
import { controlRequest } from '$repo/daemon/dist/control.js';
for (let i = 0; i < 200; i++) {
  const reqs = await controlRequest('pair.list');
  // Wait like a person would, so the device shows its pairing state before the answer.
  if (reqs.length) { await new Promise((r) => setTimeout(r, 400)); await controlRequest('pair.answer', { id: reqs[0].id, yes: true }); console.log('host code', reqs[0].code); break; }
  await new Promise((r) => setTimeout(r, 100));
}" &

# Fake merged image (bootloader, partition table at 0x8000, app at 0x10000), sent with `thronectl flash`.
img="$work/thronectl-cardputer-adv-v9.9.9.bin"
export LINKCHECK_OTA_READY="$work/ota-ready"
node --input-type=module -e "
import { writeFileSync } from 'node:fs';
import { randomBytes } from 'node:crypto';
const b = Buffer.alloc(0x10000 + 300000, 0xff);
b[0] = 0xe9;
const entry = (off, type, sub, start, size) => { b[off] = 0xaa; b[off + 1] = 0x50; b[off + 2] = type; b[off + 3] = sub; b.writeUInt32LE(start, off + 4); b.writeUInt32LE(size, off + 8); };
entry(0x8000, 1, 2, 0x9000, 0x5000);
entry(0x8020, 0, 0x10, 0x10000, 0x330000);
randomBytes(300000).copy(b, 0x10000);
b[0x10000] = 0xe9;
writeFileSync('$img', b);"
(
  for _ in $(seq 900); do [ -f "$LINKCHECK_OTA_READY" ] && break; sleep 0.1; done
  node "$repo/daemon/dist/cli.js" flash "$img" --yes | sed 's/^/     cli: /'
  node --input-type=module -e "
import { controlRequest } from '$repo/daemon/dist/control.js';
const job = (await controlRequest('ota.status'))[0];
process.exit(job && job.state === 'done' && job.dest === 'flash' ? 0 : 1);"
) &
helper=$!

"$work/linkcheck" 127.0.0.1 "$port" "$img" || { echo "--- daemon log"; cat "$work/daemon.log"; exit 1; }
wait "$helper" || { echo "FAIL daemon did not record a finished update"; cat "$work/daemon.log"; exit 1; }
echo "ok   daemon recorded the finished update"
