"""Converts a TrueType font to an Adafruit GFX font header, the same way fontconvert does.

Usage: uv run --with freetype-py python gfxfont.py FONT.ttf PIXELS NAME > out.h
"""
import math
import sys

import freetype


def convert(path: str, size: int, name: str) -> str:
    face = freetype.Face(path)
    face.set_char_size(size << 6, 0, 72, 0)
    bits: list[int] = []
    glyphs: list[str] = []
    for code in range(0x20, 0x7F):
        face.load_char(chr(code), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        g = face.glyph
        bm = g.bitmap
        offset = len(bits) // 8
        for y in range(bm.rows):
            for x in range(bm.width):
                byte = bm.buffer[y * bm.pitch + x // 8]
                bits.append((byte >> (7 - x % 8)) & 1)
        while len(bits) % 8:  # each glyph starts on a byte
            bits.append(0)
        label = "'\\\\'" if code == 0x5C else f"'{chr(code)}'"
        glyphs.append(f"  {{ {offset:5d}, {bm.width:3d}, {bm.rows:3d}, {math.ceil(g.linearHoriAdvance / 65536):3d}, "
                      f"{g.bitmap_left:3d}, {1 - g.bitmap_top:3d} }},  // 0x{code:02x} {label}")
    data = [sum(bits[i + j] << (7 - j) for j in range(8)) for i in range(0, len(bits), 8)]
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    y_advance = face.size.height >> 6
    out = [f"// {name}: {path.rsplit('/', 1)[-1]} at {size} px, made by tools/fontgen/gfxfont.py.",
           f"static const uint8_t {name}Bitmaps[] = {{", *[f"  {r}," for r in rows], "};",
           f"static const lgfx::GFXglyph {name}Glyphs[] = {{", *glyphs, "};",
           f"static const lgfx::GFXfont {name}((uint8_t*){name}Bitmaps, (lgfx::GFXglyph*){name}Glyphs, 0x20, 0x7e, {y_advance});",
           ""]
    return "\n".join(out)


if __name__ == "__main__":
    print(convert(sys.argv[1], int(sys.argv[2]), sys.argv[3]))
