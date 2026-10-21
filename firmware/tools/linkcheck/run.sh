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
  "$here/linkcheck.cpp" "$fw/src/net/link.cpp" "$fw/src/net/crypto.cpp" "$fw/src/core/settings.cpp" \
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
  if (reqs.length) { await controlRequest('pair.answer', { id: reqs[0].id, yes: true }); console.log('host code', reqs[0].code); break; }
  await new Promise((r) => setTimeout(r, 100));
}" &

"$work/linkcheck" 127.0.0.1 "$port" || { echo "--- daemon log"; cat "$work/daemon.log"; exit 1; }
