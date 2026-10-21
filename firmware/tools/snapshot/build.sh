#!/usr/bin/env bash
# Builds the host snapshot tool: renders every screen to PNG with mock services.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../.."
libs="$fw/.pio/libdeps/cardputer-adv"
[ -d "$libs/M5GFX" ] || (cd "$fw" && pio pkg install)
gfx="$libs/M5GFX/src"
out="${OUT:-$here/build}"
mkdir -p "$out"
srcs=("$here"/*.cpp "$fw"/src/core/*.cpp "$fw"/src/screens/*.cpp)
gfxsrc=("$gfx"/lgfx/v1/*.cpp "$gfx"/lgfx/utility/*.c "$gfx"/lgfx/Fonts/efont/*.c "$gfx"/lgfx/Fonts/IPA/*.c)
objs=()
for f in "${gfxsrc[@]}"; do
  o="$out/gfx_$(basename "$f").o"
  [ -f "$o" ] || { case "$f" in *.c) cc -O1 -DLGFX_LINUX_FB -I"$gfx" -c "$f" -o "$o";; *) g++ -std=c++17 -O1 -DLGFX_LINUX_FB -I"$gfx" -c "$f" -o "$o";; esac; }
  objs+=("$o")
done
g++ -std=c++17 -O1 -g -Wall -Wno-unused-parameter -DLGFX_LINUX_FB -I"$gfx" -I"$libs/ArduinoJson/src" -I"$fw/src" \
  "${srcs[@]}" "${objs[@]}" -o "$out/snapshot" -lpthread
echo "$out/snapshot"
