#pragma once
#include "platform.h"

constexpr uint16_t rgb(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xF8) << 8) | (((hex >> 8) & 0xFC) << 3) | ((hex & 0xFF) >> 3));
}

struct Theme {
  const char* name;
  uint16_t bg, panel, panel2, fg, dim, accent, accent2, ok, warn, err, sel, selFg;
};

struct Accent {
  const char* name;
  uint16_t color;  // 0 = use the theme accent
};

extern const Theme THEMES[];
extern const int THEME_COUNT;
extern const Accent ACCENTS[];
extern const int ACCENT_COUNT;

const Theme& T();
void applyTheme(int themeIdx, int accentIdx);
/** 0 small (more on screen), 1 normal (larger fonts and rows). Sets the fonts in F and the layout sizes. */
void applyTextSize(int size);
bool largeText();

uint16_t blend(uint16_t a, uint16_t b, uint8_t alpha);  // alpha 0 = a, 255 = b

namespace F {
extern const Font* small;   // 6x8 pixel font
extern const Font* text;    // proportional body text
extern const Font* bold;    // titles
extern const Font* big;     // large numbers
extern const Font* huge;    // pairing code, splash
extern const Font* tiny;    // 4x6 terminal font
extern const Font* mono;    // fixed width, for text fields
extern const Font* const glcd;  // 6x8 terminal cells, same in every text size
}  // namespace F
