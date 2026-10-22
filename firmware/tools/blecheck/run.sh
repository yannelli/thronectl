#!/usr/bin/env bash
# Builds src/hal/ble.cpp on the host against a fake NimBLE, with AddressSanitizer, and runs the checks.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../.."
libs="$fw/.pio/libdeps/cardputer-adv"
out="${OUT:-$here/build}"
mkdir -p "$out"
g++ -std=c++17 -O1 -g -Wall -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=all -DLGFX_LINUX_FB \
  -I"$here/stub" -I"$libs/M5GFX/src" -I"$libs/ArduinoJson/src" -I"$fw/src" \
  "$here/blecheck.cpp" "$fw/src/hal/ble.cpp" -o "$out/blecheck"
"$out/blecheck"
