#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../.."
out="${OUT:-$here/build}"
mkdir -p "$out"
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-sanitize-recover=all -DLGFX_LINUX_FB \
  -I"$here/stub" -I"$fw/.pio/libdeps/cardputer-adv/M5GFX/src" -I"$fw/src" \
  "$here/ledcheck.cpp" "$fw/src/hal/led.cpp" -o "$out/ledcheck"
"$out/ledcheck"
