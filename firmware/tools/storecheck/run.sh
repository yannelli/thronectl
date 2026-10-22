#!/usr/bin/env bash
# Builds the flash settings store check with AddressSanitizer and UBSan, then runs it.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../.."
libs="$fw/.pio/libdeps/cardputer-adv"
out="${OUT:-$here/build}"
mkdir -p "$out"
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-maybe-uninitialized \
  -fsanitize=address,undefined -fno-sanitize-recover=all -DLGFX_LINUX_FB \
  -I"$libs/M5GFX/src" -isystem "$libs/ArduinoJson/src" -I"$fw/src" \
  "$here/storecheck.cpp" "$fw/src/core/filestore.cpp" "$fw/src/core/settings.cpp" "$fw/src/core/led.cpp" \
  -o "$out/storecheck"
"$out/storecheck"
