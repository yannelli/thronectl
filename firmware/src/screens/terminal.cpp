#include "screens.h"
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/theme.h"

namespace {

struct Cell {
  uint32_t cp = ' ';
  uint16_t fg = 0, bg = 0;
  uint8_t flags = 0;
  uint8_t def = 3;  // bit 0: default fg, bit 1: default bg
};

enum : uint8_t { BOLD = 1, UNDER = 2, ITALIC = 4, INVERSE = 8, DIM = 16 };

uint32_t nextCp(const char*& p) {
  unsigned char c = *p++;
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = c & (0x3F >> n);
  while (n-- > 0 && (*p & 0xC0) == 0x80) cp = (cp << 6) | (*p++ & 0x3F);
  return cp;
}

// Box drawing: 2 bits per direction (0 none, 1 light, 2 heavy, 3 double), order up, right, down, left.
uint8_t boxMask(uint32_t cp) {
  auto m = [](int u, int r, int d, int l) { return (uint8_t)(u | r << 2 | d << 4 | l << 6); };
  switch (cp) {
    case 0x2500: case 0x2504: case 0x2508: case 0x254C: return m(0, 1, 0, 1);
    case 0x2501: case 0x2505: case 0x2509: case 0x254D: return m(0, 2, 0, 2);
    case 0x2502: case 0x2506: case 0x250A: case 0x254E: return m(1, 0, 1, 0);
    case 0x2503: case 0x2507: case 0x250B: case 0x254F: return m(2, 0, 2, 0);
    case 0x250C: case 0x256D: return m(0, 1, 1, 0);
    case 0x250F: return m(0, 2, 2, 0);
    case 0x2510: case 0x256E: return m(0, 0, 1, 1);
    case 0x2513: return m(0, 0, 2, 2);
    case 0x2514: case 0x2570: case 0x23BF: return m(1, 1, 0, 0);
    case 0x2517: return m(2, 2, 0, 0);
    case 0x2518: case 0x256F: return m(1, 0, 0, 1);
    case 0x251B: return m(2, 0, 0, 2);
    case 0x251C: return m(1, 1, 1, 0);
    case 0x2523: return m(2, 2, 2, 0);
    case 0x2524: return m(1, 0, 1, 1);
    case 0x252B: return m(2, 0, 2, 2);
    case 0x252C: return m(0, 1, 1, 1);
    case 0x2533: return m(0, 2, 2, 2);
    case 0x2534: return m(1, 1, 0, 1);
    case 0x253B: return m(2, 2, 0, 2);
    case 0x253C: return m(1, 1, 1, 1);
    case 0x254B: return m(2, 2, 2, 2);
    case 0x2550: return m(0, 3, 0, 3);
    case 0x2551: return m(3, 0, 3, 0);
    case 0x2554: return m(0, 3, 3, 0);
    case 0x2557: return m(0, 0, 3, 3);
    case 0x255A: return m(3, 3, 0, 0);
    case 0x255D: return m(3, 0, 0, 3);
    case 0x2560: return m(3, 3, 3, 0);
    case 0x2563: return m(3, 0, 3, 3);
    case 0x2566: return m(0, 3, 3, 3);
    case 0x2569: return m(3, 3, 0, 3);
    case 0x256C: return m(3, 3, 3, 3);
    case 0x2574: return m(0, 0, 0, 1);
    case 0x2575: return m(1, 0, 0, 0);
    case 0x2576: return m(0, 1, 0, 0);
    case 0x2577: return m(0, 0, 1, 0);
    case 0x2578: return m(0, 0, 0, 2);
    case 0x2579: return m(2, 0, 0, 0);
    case 0x257A: return m(0, 2, 0, 0);
    case 0x257B: return m(0, 0, 2, 0);
    default: return 0;
  }
}

void drawBox(Canvas& c, int x, int y, int w, int h, uint8_t mask, uint16_t col) {
  int cx = x + w / 2 - (w > 4 ? 1 : 0), cy = y + h / 2 - 1;
  auto seg = [&](int dir, int kind) {
    if (!kind) return;
    bool vert = dir == 0 || dir == 2;
    int len0 = vert ? (dir == 0 ? y : cy) : (dir == 3 ? x : cx);
    int len1 = vert ? (dir == 0 ? cy : y + h - 1) : (dir == 3 ? cx : x + w - 1);
    for (int o = kind == 3 ? -1 : 0; o <= (kind == 1 ? 0 : 1); o += kind == 3 ? 2 : 1) {
      if (vert) c.drawFastVLine(cx + o, len0, len1 - len0 + 1, col);
      else c.drawFastHLine(len0, cy + o, len1 - len0 + 1, col);
    }
  };
  for (int d = 0; d < 4; d++) seg(d, (mask >> (d * 2)) & 3);
}

bool drawBlock(Canvas& c, int x, int y, int w, int h, uint32_t cp, uint16_t fg, uint16_t bg) {
  if (cp == 0x2580) c.fillRect(x, y, w, h / 2, fg);
  else if (cp >= 0x2581 && cp <= 0x2588) { int bh = h * (int)(cp - 0x2580) / 8; c.fillRect(x, y + h - bh, w, bh, fg); }
  else if (cp >= 0x2589 && cp <= 0x258F) { int bw = (w * (int)(0x2590 - cp) + 4) / 8; c.fillRect(x, y, bw ? bw : 1, h, fg); }
  else if (cp == 0x2590) c.fillRect(x + w / 2, y, w - w / 2, h, fg);
  else if (cp >= 0x2591 && cp <= 0x2593) {
    int lvl = (int)(cp - 0x2590);
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++)
        if (lvl == 2 ? ((i + j) & 1) : lvl == 1 ? ((i & 1) && (j & 1)) : !((i & 1) && (j & 1))) c.drawPixel(x + i, y + j, fg);
  } else if (cp == 0x2594) c.fillRect(x, y, w, 1, fg);
  else if (cp == 0x2595) c.fillRect(x + w - 1, y, 1, h, fg);
  else if (cp >= 0x2596 && cp <= 0x259F) {
    static const uint8_t q[] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};  // bits: 1 UL, 2 UR, 4 LL, 8 LR
    uint8_t b = q[cp - 0x2596];
    int hw = w / 2, hh = h / 2;
    if (b & 1) c.fillRect(x, y, hw, hh, fg);
    if (b & 2) c.fillRect(x + hw, y, w - hw, hh, fg);
    if (b & 4) c.fillRect(x, y + hh, hw, h - hh, fg);
    if (b & 8) c.fillRect(x + hw, y + hh, w - hw, h - hh, fg);
  } else if (cp >= 0x2800 && cp <= 0x28FF) {
    uint8_t b = cp - 0x2800;
    static const uint8_t col[] = {0, 0, 0, 1, 1, 1, 0, 1}, row[] = {0, 1, 2, 0, 1, 2, 3, 3};
    int dw = w / 2, dh = h / 4;
    for (int i = 0; i < 8; i++)
      if (b & (1 << i)) c.fillRect(x + col[i] * dw + (dw > 2 ? 1 : 0), y + row[i] * dh, dw > 2 ? dw - 1 : 1, dh > 1 ? dh - 1 : 1, fg);
  } else return false;
  return true;
}

/** Shapes for common symbols agent CLIs print. Returns false when the glyph is unknown. */
bool drawSymbol(Canvas& c, int x, int y, int w, int h, uint32_t cp, uint16_t fg) {
  int cx = x + w / 2 - (w > 4 ? 1 : 0), cy = y + h / 2 - 1, r = (w < h ? w : h) / 2 - 1;
  switch (cp) {
    case 0x25CF: case 0x23FA: case 0x2B24: c.fillCircle(cx, cy, r, fg); return true;
    case 0x2022: case 0x2219: c.fillCircle(cx, cy, r > 1 ? r - 1 : 1, fg); return true;
    case 0x00B7: case 0x22C5: c.fillRect(cx, cy, 1 + (w > 4), 1 + (w > 4), fg); return true;
    case 0x25CB: case 0x25EF: case 0x25E6: c.drawCircle(cx, cy, r, fg); return true;
    case 0x25C6: case 0x25C7: case 0x2666:
      c.drawLine(cx, cy - r, cx + r, cy, fg); c.drawLine(cx + r, cy, cx, cy + r, fg);
      c.drawLine(cx, cy + r, cx - r, cy, fg); c.drawLine(cx - r, cy, cx, cy - r, fg);
      if (cp != 0x25C7) c.fillTriangle(cx, cy - r, cx + r, cy, cx - r, cy, fg), c.fillTriangle(cx - r, cy, cx + r, cy, cx, cy + r, fg);
      return true;
    case 0x25B6: case 0x25BA: case 0x25B8: c.fillTriangle(x + 1, cy - r, x + 1, cy + r, x + w - 2, cy, fg); return true;
    case 0x25C0: case 0x25C4: c.fillTriangle(x + w - 2, cy - r, x + w - 2, cy + r, x + 1, cy, fg); return true;
    case 0x25B2: case 0x25B4: c.fillTriangle(cx, y + 1, x, y + h - 2, x + w - 1, y + h - 2, fg); return true;
    case 0x25BC: case 0x25BE: c.fillTriangle(cx, y + h - 2, x, y + 1, x + w - 1, y + 1, fg); return true;
    case 0x2713: case 0x2714: case 0x221A:
      c.drawLine(x, cy, cx - 1, y + h - 2, fg); c.drawLine(cx - 1, y + h - 2, x + w - 1, y + 1, fg); return true;
    case 0x2717: case 0x2718: case 0x00D7: case 0x2715: case 0x2716:
      c.drawLine(x + 1, cy - r, x + w - 2, cy + r, fg); c.drawLine(x + w - 2, cy - r, x + 1, cy + r, fg); return true;
    case 0x2026: for (int i = 0; i < 3; i++) c.drawPixel(x + i * w / 3 + 1, y + h - 2, fg); return true;
    case 0x2191: c.drawFastVLine(cx, y + 1, h - 2, fg); c.drawLine(cx, y + 1, cx - 2, y + 3, fg); c.drawLine(cx, y + 1, cx + 2, y + 3, fg); return true;
    case 0x2193: c.drawFastVLine(cx, y + 1, h - 2, fg); c.drawLine(cx, y + h - 2, cx - 2, y + h - 4, fg); c.drawLine(cx, y + h - 2, cx + 2, y + h - 4, fg); return true;
    default: return false;
  }
}

char asciiFallback(uint32_t cp) {
  switch (cp) {
    case 0x276F: case 0x203A: case 0x2192: case 0x279C: case 0x27A4: case 0x2794: case 0x00BB: return '>';
    case 0x2039: case 0x2190: case 0x00AB: return '<';
    case 0x2014: case 0x2013: case 0x2212: case 0x2010: return '-';
    case 0x2018: case 0x2019: case 0x00B4: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x26A0: case 0x2757: return '!';
    case 0x00A0: case 0x2003: case 0x2002: case 0x200B: return ' ';
    case 0x2261: case 0x2630: return '=';
    default: break;
  }
  if ((cp >= 0x2720 && cp <= 0x274B) || cp == 0x2605 || cp == 0x2606 || cp == 0x2726 || cp == 0x2727) return '*';
  if (cp >= 0x2800 && cp <= 0x28FF) return ' ';
  return cp < 0x80 ? (char)cp : '?';
}

class Terminal : public Screen {
 public:
  bool typing() const override { return true; }
  Terminal(std::string agent, std::string title) : agent_(std::move(agent)), title_(std::move(title)) {
    small_ = settings().termFont == 0;
    cols_ = small_ ? 60 : 40;
    rows_ = small_ ? 20 : 15;
    cw_ = small_ ? 4 : 6;
    ch_ = small_ ? 6 : 8;
    grid_.assign(cols_ * rows_, Cell());
  }

  ~Terminal() override {
    if (tid_ && svc().link->ready()) {
      JsonDocument p;
      p["tid"] = tid_;
      svc().link->call("term.close", p, [](bool, JsonVariantConst, const char*) {});
    }
  }

  bool statusBar() const override { return false; }

  void onEnter() override {
    if (!svc().link->ready()) {
      state_ = "not connected";
      return;
    }
    app().termSink = guard([this](const char* ev, JsonVariantConst d) { onEvent(ev, d); });
    JsonDocument p;
    if (agent_.empty()) {
      p["shell"] = true;
      p["cwd"] = "~";
    } else p["agent"] = agent_.c_str();
    p["cols"] = cols_;
    p["rows"] = rows_;
    svc().link->call("term.open", p, guard([this](bool ok, JsonVariantConst r, const char* err) {
      if (!ok) {
        state_ = err ? err : "failed";
        return;
      }
      tid_ = r["tid"] | 0;
      state_.clear();
    }), 15000);
  }

  void update(uint32_t) override {
    if (!out_.empty() && tid_ && svc().link->ready()) {
      JsonDocument p;
      p["tid"] = tid_;
      p["d"] = out_.c_str();
      svc().link->call("term.input", p, [](bool, JsonVariantConst, const char*) {});
      out_.clear();
    }
  }

  bool onKey(const KeyEvent& e) override {
    // The Esc keycap leaves; fn+` sends Esc to the host and Opt+` types a backtick.
    if ((e.opt && e.key == 'q') || (e.key == '`' && e.plain())) { leave(); return true; }
    if (exited_ || !tid_) {
      if (isBack(e) || e.key == K_ENTER) ui().pop();
      return true;
    }
    out_ += encode(e);
    return true;
  }

  bool onVoiceText(const std::string& text) override {
    if (tid_ && !exited_) out_ += text;
    return true;
  }

  std::string voiceTarget() const override { return "type into " + title_; }

  void draw(Canvas& c) override {
    const Theme& t = T();
    c.fillRect(0, 0, SCREEN_W, 10, t.panel);
    gfx::text(c, F::glcd, 3, 1, gfx::ellipsize(c, F::glcd, title_, 150), t.accent);
    std::string right = !state_.empty() ? state_ : exited_ ? "exited" : "` back  fn+` esc";
    gfx::text(c, F::glcd, SCREEN_W - 3, 1, gfx::ellipsize(c, F::glcd, right, 120), state_.empty() && !exited_ ? t.dim : t.warn,
              gfx::Datum::top_right);
    if (!tid_ && state_.empty()) gfx::spinner(c, SCREEN_W / 2, 70, 8, nowMs(), t.accent);
    drawGrid(c, t);
  }

 private:
  /** A second press within 2 s closes a running shell, so one stray Esc does not end it. */
  void leave() {
    if (!agent_.empty() || !tid_ || exited_ || elapsedMs(nowMs(), escAt_) < 2000) return ui().pop();
    escAt_ = nowMs();
    ui().toast("info", "Press ` again to close the shell", "", 2000);
  }

  std::string encode(const KeyEvent& e) {
    switch (e.key) {
      case K_UP: return e.shift ? "\x1b[5~" : "\x1b[A";
      case K_DOWN: return e.shift ? "\x1b[6~" : "\x1b[B";
      case K_RIGHT: return e.shift ? "\x1b[F" : e.alt ? "\x1b" "f" : "\x1b[C";
      case K_LEFT: return e.shift ? "\x1b[H" : e.alt ? "\x1b" "b" : "\x1b[D";
      case K_ENTER: return e.alt ? "\x1b\r" : "\r";
      case K_BKSP: return e.alt ? "\x1b\x7f" : "\x7f";
      case K_DEL: return "\x1b[3~";
      case K_ESC: return "\x1b";
      case K_TAB: return e.shift ? "\x1b[Z" : "\t";
      default: break;
    }
    if (e.key >= K_F1 && e.key <= K_F12) {
      static const char* const fk[] = {"\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS", "\x1b[15~", "\x1b[17~",
                                       "\x1b[18~", "\x1b[19~", "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~"};
      return fk[e.key - K_F1];
    }
    if (!e.isChar()) return "";
    char ch = e.ch();
    if (e.ctrl) {
      if (ch >= 'a' && ch <= 'z') ch -= 0x60;
      else if (ch >= '@' && ch <= '_') ch -= 0x40;
      else if (ch == ' ' || ch == '2') ch = 0;
      else if (ch == '/') ch = 0x1f;
    }
    std::string s(1, ch);
    return e.alt ? "\x1b" + s : s;
  }

  void onEvent(const char* ev, JsonVariantConst d) {
    int tid = d["tid"] | 0;
    if (!tid_ || tid != tid_) return;
    if (!strcmp(ev, "term_exit")) {
      exited_ = true;
      int code = d["code"] | 0;
      state_ = "exited " + std::to_string(code) + "  ent close";
      return;
    }
    if (d["full"] | false) grid_.assign(cols_ * rows_, Cell());
    cx_ = d["cx"] | cx_;
    cy_ = d["cy"] | cy_;
    cv_ = d["cv"] | cv_;
    for (JsonArrayConst row : d["rows"].as<JsonArrayConst>()) {
      int y = row[0] | -1;
      if (y < 0 || y >= rows_) continue;
      Cell* line = &grid_[y * cols_];
      int x = 0;
      for (JsonArrayConst span : row[1].as<JsonArrayConst>()) {
        int fg = span[0] | -1, bg = span[1] | -1;
        uint8_t flags = span[2] | 0;
        const char* p = span[3] | "";
        while (*p && x < cols_) {
          Cell& cell = line[x++];
          cell.cp = nextCp(p);
          cell.fg = fg < 0 ? 0 : (uint16_t)fg;
          cell.bg = bg < 0 ? 0 : (uint16_t)bg;
          cell.def = (fg < 0 ? 1 : 0) | (bg < 0 ? 2 : 0);
          cell.flags = flags;
        }
      }
      while (x < cols_) line[x++] = Cell();
    }
  }

  void drawGrid(Canvas& c, const Theme& t) {
    const Font* font = small_ ? F::tiny : F::glcd;
    c.setFont(font);
    c.setTextDatum(lgfx::textdatum_t::top_left);
    int y0 = 10;
    char run[64];
    for (int y = 0; y < rows_; y++) {
      int py = y0 + y * ch_;
      int x = 0;
      while (x < cols_) {
        const Cell& a = grid_[y * cols_ + x];
        uint16_t fg = a.def & 1 ? t.fg : a.fg, bg = a.def & 2 ? t.bg : a.bg;
        if (a.flags & INVERSE) std::swap(fg, bg);
        if (a.flags & DIM) fg = blend(fg, bg, 110);
        bool bgDefault = !(a.flags & INVERSE) && (a.def & 2);
        int n = 0;
        if (!special(a.cp)) {
          while (x + n < cols_ && n < 63) {
            const Cell& b = grid_[y * cols_ + x + n];
            if (b.fg != a.fg || b.bg != a.bg || b.def != a.def || b.flags != a.flags || special(b.cp)) break;
            run[n++] = asciiFallback(b.cp);
          }
        }
        if (n == 0) {
          int px = x * cw_;
          if (!bgDefault) c.fillRect(px, py, cw_, ch_, bg);
          if (uint8_t m = boxMask(a.cp)) drawBox(c, px, py, cw_, ch_, m, fg);
          else if (!drawBlock(c, px, py, cw_, ch_, a.cp, fg, bg)) drawSymbol(c, px, py, cw_, ch_, a.cp, fg);
          x++;
          continue;
        }
        run[n] = 0;
        int px = x * cw_;
        if (!bgDefault) c.fillRect(px, py, n * cw_, ch_, bg);
        c.setTextColor(fg);
        c.drawString(run, px, py);
        if (a.flags & BOLD && !small_) c.drawString(run, px + 1, py);
        if (a.flags & UNDER) c.drawFastHLine(px, py + ch_ - 1, n * cw_, fg);
        x += n;
      }
    }
    if (cv_ && tid_ && !exited_ && cx_ < cols_ && cy_ < rows_ && (nowMs() / 530) % 2 == 0) {
      int px = cx_ * cw_, py = y0 + cy_ * ch_;
      c.fillRect(px, py + ch_ - 2, cw_, 2, t.accent);
    }
  }

  /** Glyphs drawn as shapes instead of font characters. */
  static bool special(uint32_t cp) {
    if (cp < 0x80) return false;
    if (boxMask(cp)) return true;
    switch (cp) {
      case 0x25CF: case 0x23FA: case 0x2B24: case 0x2022: case 0x2219: case 0x00B7: case 0x22C5: case 0x25CB:
      case 0x25EF: case 0x25E6: case 0x25C6: case 0x25C7: case 0x2666: case 0x25B6: case 0x25BA: case 0x25B8:
      case 0x25C0: case 0x25C4: case 0x25B2: case 0x25B4: case 0x25BC: case 0x25BE: case 0x2713: case 0x2714:
      case 0x221A: case 0x2717: case 0x2718: case 0x00D7: case 0x2715: case 0x2716: case 0x2026: case 0x2191:
      case 0x2193: return true;
      default: return (cp >= 0x2580 && cp <= 0x259F) || (cp >= 0x2800 && cp <= 0x28FF);
    }
  }

  std::string agent_, title_, state_, out_;
  bool small_ = false, exited_ = false, cv_ = true;
  uint32_t escAt_ = 0x80000000u;
  int cols_, rows_, cw_, ch_, cx_ = 0, cy_ = 0;
  int tid_ = 0;
  std::vector<Cell> grid_;
};

}  // namespace

Screen* makeTerminal(const std::string& agentId, const std::string& title) { return new Terminal(agentId, title); }
