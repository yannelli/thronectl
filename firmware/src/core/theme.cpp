#include "theme.h"
#include "fonts_medium.h"

//                 name            bg              panel           panel2          fg              dim             accent          accent2         ok              warn            err             sel             selFg
const Theme THEMES[] = {
  {"Throne",       rgb(0x0E0B16), rgb(0x1A1626), rgb(0x262035), rgb(0xECE6F7), rgb(0x8A8199), rgb(0xF5C542), rgb(0x9B7BFF), rgb(0x4ADE80), rgb(0xFBBF24), rgb(0xF87171), rgb(0x2E2645), rgb(0xFFFFFF)},
  {"Midnight",     rgb(0x0B1220), rgb(0x121A2B), rgb(0x1B2540), rgb(0xE2E8F0), rgb(0x7C8BA1), rgb(0x38BDF8), rgb(0x818CF8), rgb(0x34D399), rgb(0xFBBF24), rgb(0xF87171), rgb(0x1E2A47), rgb(0xFFFFFF)},
  {"Tokyo Night",  rgb(0x1A1B26), rgb(0x1F2335), rgb(0x292E42), rgb(0xC0CAF5), rgb(0x565F89), rgb(0x7AA2F7), rgb(0xBB9AF7), rgb(0x9ECE6A), rgb(0xE0AF68), rgb(0xF7768E), rgb(0x2F3549), rgb(0xFFFFFF)},
  {"Catppuccin",   rgb(0x1E1E2E), rgb(0x181825), rgb(0x313244), rgb(0xCDD6F4), rgb(0x7F849C), rgb(0xCBA6F7), rgb(0x89B4FA), rgb(0xA6E3A1), rgb(0xF9E2AF), rgb(0xF38BA8), rgb(0x45475A), rgb(0xFFFFFF)},
  {"Dracula",      rgb(0x282A36), rgb(0x21222C), rgb(0x343746), rgb(0xF8F8F2), rgb(0x6272A4), rgb(0xBD93F9), rgb(0xFF79C6), rgb(0x50FA7B), rgb(0xF1FA8C), rgb(0xFF5555), rgb(0x44475A), rgb(0xFFFFFF)},
  {"Nord",         rgb(0x2E3440), rgb(0x3B4252), rgb(0x434C5E), rgb(0xECEFF4), rgb(0x7B88A1), rgb(0x88C0D0), rgb(0x81A1C1), rgb(0xA3BE8C), rgb(0xEBCB8B), rgb(0xBF616A), rgb(0x4C566A), rgb(0xFFFFFF)},
  {"Gruvbox",      rgb(0x282828), rgb(0x32302F), rgb(0x3C3836), rgb(0xEBDBB2), rgb(0x928374), rgb(0xFE8019), rgb(0xFABD2F), rgb(0xB8BB26), rgb(0xFABD2F), rgb(0xFB4934), rgb(0x504945), rgb(0xFBF1C7)},
  {"Solarized",    rgb(0x002B36), rgb(0x073642), rgb(0x0A4655), rgb(0xEEE8D5), rgb(0x839496), rgb(0x268BD2), rgb(0x2AA198), rgb(0x859900), rgb(0xB58900), rgb(0xDC322F), rgb(0x0E5263), rgb(0xFDF6E3)},
  {"Synthwave",    rgb(0x1A1033), rgb(0x241447), rgb(0x2E1A5C), rgb(0xFDF0FF), rgb(0x9A7FBF), rgb(0xFF2E97), rgb(0x00E5FF), rgb(0x72F1B8), rgb(0xFEDE5D), rgb(0xFE4450), rgb(0x3D2373), rgb(0xFFFFFF)},
  {"Matrix",       rgb(0x000800), rgb(0x001A00), rgb(0x002A00), rgb(0xB6FFB6), rgb(0x3D7A3D), rgb(0x00FF41), rgb(0x7CFC00), rgb(0x00FF41), rgb(0xD4FF00), rgb(0xFF4040), rgb(0x003D00), rgb(0xE8FFE8)},
  {"Amber CRT",    rgb(0x120A00), rgb(0x1E1200), rgb(0x2A1A00), rgb(0xFFB000), rgb(0x8A5F00), rgb(0xFFCC00), rgb(0xFF8C00), rgb(0xFFD54F), rgb(0xFFA000), rgb(0xFF5722), rgb(0x3A2400), rgb(0xFFE0A0)},
  {"Paper",        rgb(0xF5F1E8), rgb(0xE8E2D4), rgb(0xDCD4C2), rgb(0x1F1A14), rgb(0x7A705F), rgb(0xC2410C), rgb(0x1D4ED8), rgb(0x15803D), rgb(0xB45309), rgb(0xB91C1C), rgb(0xD6CCB4), rgb(0x000000)},
  // "Absolutely": claude.ai light and dark tokens with the Claude clay accent, plus a near-black variant.
  {"Absolutely Light", rgb(0xFAF9F5), rgb(0xF0EEE6), rgb(0xE3E1D8), rgb(0x141413), rgb(0x73726C), rgb(0xC6613F), rgb(0x2C84DB), rgb(0x2F7613), rgb(0x875A08), rgb(0xB53333), rgb(0xE8E6DC), rgb(0x141413)},
  {"Absolutely Dark",  rgb(0x262624), rgb(0x30302E), rgb(0x3D3D3A), rgb(0xFAF9F5), rgb(0x9C9A92), rgb(0xD97757), rgb(0x74ABE2), rgb(0x65BB30), rgb(0xDA9E25), rgb(0xFE8181), rgb(0x41403C), rgb(0xFAF9F5)},
  {"Absolutely Black", rgb(0x141413), rgb(0x1F1E1D), rgb(0x2C2C2A), rgb(0xF8F8F6), rgb(0x97958C), rgb(0xD97757), rgb(0x74ABE2), rgb(0x65BB30), rgb(0xDA9E25), rgb(0xFE8181), rgb(0x30302E), rgb(0xFFFFFF)},
};
const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

const Accent ACCENTS[] = {
  {"Theme", 0},
  {"Gold", rgb(0xF5C542)},
  {"Cyan", rgb(0x22D3EE)},
  {"Magenta", rgb(0xF472B6)},
  {"Lime", rgb(0xA3E635)},
  {"Orange", rgb(0xFB923C)},
  {"Red", rgb(0xF87171)},
  {"Blue", rgb(0x60A5FA)},
  {"Violet", rgb(0xA78BFA)},
  {"White", rgb(0xF5F5F5)},
  {"Clay", rgb(0xD97757)},
};
const int ACCENT_COUNT = sizeof(ACCENTS) / sizeof(ACCENTS[0]);

static Theme current = THEMES[0];

const Theme& T() { return current; }

void applyTheme(int themeIdx, int accentIdx) {
  if (themeIdx < 0 || themeIdx >= THEME_COUNT) themeIdx = 0;
  current = THEMES[themeIdx];
  if (accentIdx > 0 && accentIdx < ACCENT_COUNT) current.accent = ACCENTS[accentIdx].color;
}

uint16_t blend(uint16_t a, uint16_t b, uint8_t alpha) {
  uint32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint32_t r = (ar * (255 - alpha) + br * alpha) / 255;
  uint32_t g = (ag * (255 - alpha) + bg * alpha) / 255;
  uint32_t bl = (ab * (255 - alpha) + bb * alpha) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

namespace F {
const Font* small = &lgfx::fonts::Font0;
const Font* text = &lgfx::fonts::DejaVu9;
const Font* bold = &lgfx::fonts::DejaVu12;
const Font* big = &lgfx::fonts::DejaVu18;
const Font* huge = &lgfx::fonts::DejaVu24;
const Font* tiny = &lgfx::fonts::TomThumb;
const Font* mono = &lgfx::fonts::Font0;
const Font* const glcd = &lgfx::fonts::Font0;
}  // namespace F

static int g_scale = 0;
int textScale() { return g_scale; }

void applyTextSize(int size) {
  g_scale = size >= 0 && size <= 2 ? size : 0;
  using namespace lgfx::fonts;
  struct Size {
    const Font *small, *text, *bold, *big, *mono;
    int status, hint, header, line, pad;
  };
  static const Size SIZES[] = {
      {&Font0, &DejaVu9, &DejaVu12, &DejaVu18, &Font0, 12, 10, 16, 11, 0},
      {&DejaVu9, &DejaVu11, &DejaVuBold12, &DejaVu18, &DejaVuMono11, 13, 11, 18, 13, 2},
      {&DejaVu9, &DejaVu12, &FreeSansBold9pt7b, &DejaVu24, &AsciiFont8x16, 14, 12, 21, 14, 4},
  };
  const Size& z = SIZES[g_scale];
  F::small = z.small;
  F::text = z.text;
  F::bold = z.bold;
  F::big = z.big;
  F::mono = z.mono;
  STATUS_H = z.status;
  HINT_H = z.hint;
  HEADER_H = z.header;
  LINE_H = z.line;
  ROW_PAD = z.pad;
}
