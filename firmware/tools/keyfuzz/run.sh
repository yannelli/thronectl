#!/usr/bin/env bash
# Builds the key fuzzer with AddressSanitizer and UBSan, then runs it. Args: [steps] [seed]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../.."
libs="$fw/.pio/libdeps/cardputer-adv"
[ -d "$libs/M5GFX" ] || (cd "$fw" && pio pkg install)
gfx="$libs/M5GFX/src"
out="${OUT:-$here/build}"
mkdir -p "$out"
gfxsrc=("$gfx"/lgfx/v1/*.cpp "$gfx"/lgfx/utility/*.c "$gfx"/lgfx/Fonts/efont/*.c "$gfx"/lgfx/Fonts/IPA/*.c)
objs=()
for f in "${gfxsrc[@]}"; do
  o="$out/gfx_$(basename "$f").o"
  [ -f "$o" ] || { case "$f" in *.c) cc -O1 -DLGFX_LINUX_FB -I"$gfx" -c "$f" -o "$o";; *) g++ -std=c++17 -O1 -DLGFX_LINUX_FB -I"$gfx" -c "$f" -o "$o";; esac; }
  objs+=("$o")
done
san="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
g++ -std=c++17 -O1 -g $san -DLGFX_LINUX_FB -I"$gfx" -I"$libs/ArduinoJson/src" -I"$fw/src" \
  "$here/keyfuzz.cpp" "$fw"/src/core/*.cpp "$fw"/src/screens/*.cpp "${objs[@]}" -o "$out/keyfuzz" -lpthread
ASAN_OPTIONS=detect_leaks=0 "$out/keyfuzz" "${1:-200000}" "${2:-1}"
