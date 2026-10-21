#!/usr/bin/env bash
# Builds the firmware and writes one image to flash at offset 0x0 (bootloader, partitions, app).
set -euo pipefail
fw="$(cd "$(dirname "$0")/.." && pwd)"
cd "$fw"
pio run -e cardputer-adv
ver="$(sed -n 's/#define FW_VERSION "\(.*\)"/\1/p' src/core/platform.h)"
b=.pio/build/cardputer-adv
mkdir -p dist
out="dist/thronectl-cardputer-adv-v$ver.bin"
# PlatformIO's own Python has pyserial, which esptool imports.
py="$(head -1 "$(command -v pio)" | sed 's/^#!//')"
"$py" ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 merge_bin -o "$out" \
  --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 "$b/bootloader.bin" 0x8000 "$b/partitions.bin" \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 "$b/firmware.bin"
(cd dist && sha256sum "$(basename "$out")" > "$(basename "$out").sha256")
echo "$fw/$out"
