#include "gfx.h"
#include <cmath>
#include "theme.h"

namespace gfx {

void text(Canvas& c, const Font* f, int x, int y, const std::string& s, uint16_t color, Datum d) {
  c.setFont(f);
  c.setTextDatum(d);
  c.setTextColor(color);
  c.drawString(s.c_str(), x, y);
}

int width(Canvas& c, const Font* f, const std::string& s) {
  c.setFont(f);
  return c.textWidth(s.c_str());
}

int height(Canvas& c, const Font* f) {
  c.setFont(f);
  return c.fontHeight();
}

std::string ellipsize(Canvas& c, const Font* f, const std::string& s, int maxW) {
  c.setFont(f);
  if (c.textWidth(s.c_str()) <= maxW) return s;
  std::string out = s;
  while (!out.empty()) {
    out.pop_back();
    while (!out.empty() && (out.back() & 0xC0) == 0x80) out.pop_back();
    std::string t = out + "..";
    if (c.textWidth(t.c_str()) <= maxW) return t;
  }
  return "";
}

std::vector<std::string> wrap(Canvas& c, const Font* f, const std::string& s, int maxW, int maxLines) {
  c.setFont(f);
  std::vector<std::string> lines;
  size_t i = 0;
  while (i <= s.size() && (int)lines.size() < maxLines) {
    size_t nl = s.find('\n', i);
    std::string para = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
    std::string line;
    size_t p = 0;
    while (p < para.size()) {
      size_t sp = para.find(' ', p);
      std::string word = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p + 1);
      std::string trial = line + word;
      if (!line.empty() && c.textWidth(trial.c_str()) > maxW) {
        lines.push_back(line);
        line.clear();
        if ((int)lines.size() >= maxLines) return lines;
      }
      while (line.empty() && c.textWidth(word.c_str()) > maxW && word.size() > 1) {
        size_t cut = word.size();
        while (cut > 1 && c.textWidth(word.substr(0, cut).c_str()) > maxW) cut--;
        lines.push_back(word.substr(0, cut));
        word = word.substr(cut);
        if ((int)lines.size() >= maxLines) return lines;
      }
      line += word;
      if (sp == std::string::npos) break;
      p = sp + 1;
    }
    lines.push_back(line);
    if (nl == std::string::npos) break;
    i = nl + 1;
  }
  return lines;
}

void bar(Canvas& c, int x, int y, int w, int h, float frac, uint16_t fg, uint16_t bg) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  int r = h / 2;
  c.fillRoundRect(x, y, w, h, r, bg);
  int fw = (int)lroundf(frac * w);
  if (fw > 0) c.fillRoundRect(x, y, fw < h ? h : fw, h, r, fg);
}

uint16_t loadColor(float frac) {
  const Theme& t = T();
  if (frac < 0.6f) return t.ok;
  if (frac < 0.85f) return t.warn;
  return t.err;
}

void spark(Canvas& c, int x, int y, int w, int h, const float* hist, int len, int pos, int cap, float maxV, uint16_t color) {
  if (len < 2) return;
  int n = len < w ? len : w;
  float step = (float)w / (n - 1);
  int px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    int idx = (pos - n + i + cap * 2) % cap;
    float v = hist[idx] / maxV;
    if (v > 1) v = 1;
    int sx = x + (int)lroundf(i * step);
    int sy = y + h - 1 - (int)lroundf(v * (h - 1));
    c.drawFastVLine(sx, sy, y + h - sy, blend(T().panel, color, 70));
    if (px >= 0) c.drawLine(px, py, sx, sy, color);
    px = sx;
    py = sy;
  }
}

void wifi(Canvas& c, int x, int y, int rssi, bool connected, uint16_t color, uint16_t off) {
  int bars = !connected ? 0 : rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int bh = 2 + i * 2;
    c.fillRect(x + i * 3, y + 8 - bh, 2, bh, i < bars ? color : off);
  }
}

void battery(Canvas& c, int x, int y, int pct, bool charging, uint16_t color, uint16_t outline) {
  c.drawRoundRect(x, y, 16, 8, 2, outline);
  c.fillRect(x + 16, y + 2, 2, 4, outline);
  if (pct >= 0) {
    int w = (pct * 12 + 50) / 100;
    c.fillRect(x + 2, y + 2, w, 4, pct <= 15 ? T().err : color);
  }
  if (charging) {
    c.fillTriangle(x + 9, y + 1, x + 5, y + 4, x + 8, y + 4, T().warn);
    c.fillTriangle(x + 7, y + 7, x + 11, y + 4, x + 8, y + 4, T().warn);
  }
}

void spinner(Canvas& c, int cx, int cy, int r, uint32_t t, uint16_t color) {
  float a0 = (t % 1000) * 0.36f;
  c.fillArc(cx, cy, r, r - 2 < 1 ? 1 : r - 2, a0, a0 + 270, color);
}

void statusDot(Canvas& c, int cx, int cy, const std::string& st, uint32_t t) {
  uint16_t col = [&] {
    const Theme& th = T();
    if (st == "working") return th.accent;
    if (st == "waiting") return th.warn;
    if (st == "error") return th.err;
    if (st == "idle") return th.ok;
    return th.dim;
  }();
  if (st == "working") {
    spinner(c, cx, cy, 4, t, col);
  } else if (st == "waiting") {
    bool on = (t / 400) % 2 == 0;
    c.fillCircle(cx, cy, 4, on ? col : blend(T().bg, col, 110));
    c.drawFastVLine(cx, cy - 2, 3, T().bg);
    c.drawPixel(cx, cy + 2, T().bg);
  } else if (st == "exited") {
    c.drawCircle(cx, cy, 3, col);
  } else {
    c.fillCircle(cx, cy, 3, col);
  }
}

void mic(Canvas& c, int cx, int cy, int h, uint16_t color) {
  int w = h / 2;
  c.fillRoundRect(cx - w / 2, cy - h / 2, w, h * 2 / 3, w / 2, color);
  c.drawArc(cx, cy - h / 6, w * 3 / 4 + 1, w * 3 / 4, 0, 180, color);
  c.drawFastVLine(cx, cy + h / 3 - 1, h / 6 + 1, color);
  c.drawFastHLine(cx - w / 3, cy + h / 2, w * 2 / 3 + 1, color);
}

int chip(Canvas& c, int x, int y, const std::string& s, uint16_t fg, uint16_t bg) {
  c.setFont(F::small);
  int w = c.textWidth(s.c_str()) + 6;
  c.fillRoundRect(x, y, w, 10, 3, bg);
  text(c, F::small, x + 3, y + 1, s, fg);
  return w;
}

void hints(Canvas& c, std::initializer_list<Hint> items) {
  const Theme& t = T();
  int y = SCREEN_H - HINT_H;
  c.fillRect(0, y, SCREEN_W, HINT_H, t.panel);
  int x = 3;
  c.setFont(F::small);
  for (const Hint& h : items) {
    int kw = c.textWidth(h.key) + 4;
    int lw = c.textWidth(h.label);
    if (x + kw + 2 + lw > SCREEN_W - 2) break;
    c.fillRoundRect(x, y + 1, kw, 8, 2, t.panel2);
    text(c, F::small, x + 2, y + 1, h.key, t.accent);
    text(c, F::small, x + kw + 2, y + 1, h.label, t.dim);
    x += kw + 2 + lw + 6;
  }
}

void header(Canvas& c, const std::string& title, const std::string& right) {
  const Theme& t = T();
  int y = STATUS_H;
  c.fillRect(0, y, SCREEN_W, 16, t.panel2);
  c.fillRect(0, y, 3, 16, t.accent);
  int rw = right.empty() ? 0 : width(c, F::small, right) + 6;
  text(c, F::bold, 7, y + 2, ellipsize(c, F::bold, title, SCREEN_W - 12 - rw), t.fg);
  if (rw) text(c, F::small, SCREEN_W - 4, y + 4, right, t.dim, Datum::top_right);
}

void emptyState(Canvas& c, const char* title, const char* body) {
  const Theme& t = T();
  int cy = (STATUS_H + 16 + SCREEN_H - HINT_H) / 2;
  text(c, F::bold, SCREEN_W / 2, cy - 12, title, t.fg, Datum::top_center);
  auto lines = wrap(c, F::text, body, SCREEN_W - 30, 3);
  for (size_t i = 0; i < lines.size(); i++) text(c, F::text, SCREEN_W / 2, cy + 4 + (int)i * 11, lines[i], t.dim, Datum::top_center);
}

}  // namespace gfx
