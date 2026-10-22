#!/usr/bin/env bash
# Regenerates src/core/fonts_medium.h from the DejaVu fonts. Needs uv (for freetype-py).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
D=${DEJAVU:-/usr/share/fonts/truetype/dejavu}
gen() { uv run -q --with freetype-py python "$here/gfxfont.py" "$@"; }
{
  echo "#pragma once"
  echo "// Medium text size fonts (Settings > Display > Text size). Regenerate with tools/fontgen/make.sh."
  echo "#include <lgfx/v1/lgfx_fonts.hpp>"
  echo
  gen "$D/DejaVuSans.ttf" 11 DejaVu11
  gen "$D/DejaVuSans-Bold.ttf" 12 DejaVuBold12
  gen "$D/DejaVuSansMono.ttf" 11 DejaVuMono11
} > "$here/../../src/core/fonts_medium.h"
