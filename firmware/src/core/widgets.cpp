#include "widgets.h"
#include "gfx.h"
#include "theme.h"

bool ListView::onKey(const KeyEvent& e) {
  if (count == 0) return false;
  int old = sel;
  switch (navKey(e)) {
    case K_UP: sel = sel > 0 ? sel - 1 : count - 1; break;
    case K_DOWN: sel = sel < count - 1 ? sel + 1 : 0; break;
    case K_LEFT: if (e.fn) sel -= visible(); else return false; break;
    case K_RIGHT: if (e.fn) sel += visible(); else return false; break;
    default: return false;
  }
  clamp();
  return sel != old;
}

void ListView::clamp() {
  if (sel >= count) sel = count - 1;
  if (sel < 0) sel = 0;
  int vis = visible();
  if (sel < first) first = sel;
  if (sel >= first + vis) first = sel - vis + 1;
  if (first > count - vis) first = count - vis;
  if (first < 0) first = 0;
}

void ListView::draw(Canvas& c, const std::function<void(Canvas&, int, int, int, int, int, bool)>& row) {
  clamp();
  const Theme& t = T();
  int vis = visible();
  bool scroll = count > vis;
  int w = SCREEN_W - (scroll ? 4 : 0);
  for (int i = 0; i < vis && first + i < count; i++) {
    int idx = first + i;
    int rh = rowHeight(), ry = y + i * rh;
    bool s = idx == sel;
    if (s) {
      c.fillRect(0, ry, w, rh, t.sel);
      c.fillRect(0, ry, 2, rh, t.accent);
    }
    row(c, idx, 0, ry + ROW_PAD / 2, w, rh - ROW_PAD, s);
  }
  if (scroll) {
    int th = h * vis / count;
    if (th < 6) th = 6;
    int ty = y + (h - th) * first / (count - vis);
    c.fillRect(SCREEN_W - 3, y, 2, h, t.panel);
    c.fillRect(SCREEN_W - 3, ty, 2, th, t.dim);
  }
}

void TextInput::set(const std::string& s) {
  text = s.substr(0, maxLen);
  cursor = text.size();
}

void TextInput::insert(const std::string& s) {
  std::string add = s.substr(0, maxLen > text.size() ? maxLen - text.size() : 0);
  text.insert(cursor, add);
  cursor += add.size();
}

bool TextInput::onKey(const KeyEvent& e) {
  if (e.key == K_ENTER && multiline && e.shift) {
    insert("\n");
    return true;
  }
  if (e.key == K_BKSP) {
    if (cursor > 0) {
      size_t n = 1;
      while (cursor - n > 0 && (text[cursor - n] & 0xC0) == 0x80) n++;
      text.erase(cursor - n, n);
      cursor -= n;
    }
    return true;
  }
  if (e.key == K_DEL) {
    if (cursor < text.size()) text.erase(cursor, 1);
    return true;
  }
  if (e.key == K_LEFT) { if (cursor > 0) cursor--; return true; }
  if (e.key == K_RIGHT) { if (cursor < text.size()) cursor++; return true; }
  if (e.key == K_UP && e.fn) { cursor = 0; return true; }
  if (e.key == K_DOWN && e.fn) { cursor = text.size(); return true; }
  if (e.key == K_TAB && multiline) { insert("  "); return true; }
  if (e.key == '`' && !e.opt) return false;  // Esc keycap: the screen goes back
  if (e.isChar() && !e.ctrl && !e.alt && !e.fn) {
    if (text.size() < maxLen) insert(std::string(1, e.ch()));
    return true;
  }
  return false;
}

void TextInput::draw(Canvas& c, int x, int y, int w, int h, bool focused) {
  const Theme& t = T();
  c.fillRoundRect(x, y, w, h, 3, t.panel);
  c.drawRoundRect(x, y, w, h, 3, focused ? t.accent : t.panel2);
  c.setFont(F::mono);
  const int cw = c.textWidth("M"), lh = c.fontHeight() + 1;
  int cols = (w - 6) / cw, rows = (h - 4) / lh;
  if (cols < 1 || rows < 1) return;
  std::string shown = mask ? std::string(text.size(), '*') : text;
  std::vector<size_t> starts{0};
  int col = 0;
  for (size_t i = 0; i < shown.size(); i++) {
    if (shown[i] == '\n' && multiline) { starts.push_back(i + 1); col = 0; continue; }
    if (multiline && col >= cols) { starts.push_back(i); col = 0; }
    col++;
  }
  int curLine = 0;
  while (curLine + 1 < (int)starts.size() && starts[curLine + 1] <= cursor) curLine++;
  int curCol = (int)(cursor - starts[curLine]);
  auto lineText = [&](int l) {
    size_t a = starts[l], b = l + 1 < (int)starts.size() ? starts[l + 1] : shown.size();
    std::string s = shown.substr(a, b - a);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
  };
  std::vector<std::string> lines;
  for (int l = 0; l < (int)starts.size(); l++) lines.push_back(lineText(l));
  int firstLine = curLine >= rows ? curLine - rows + 1 : 0;
  int hScroll = (!multiline && curCol >= cols) ? curCol - cols + 1 : 0;
  for (int r = 0; r < rows && firstLine + r < (int)lines.size(); r++) {
    std::string s = lines[firstLine + r];
    if (hScroll) s = s.substr(hScroll);
    gfx::text(c, F::mono, x + 3, y + 3 + r * lh, s.substr(0, cols), t.fg);
  }
  if (focused && (nowMs() / 500) % 2 == 0) {
    int cx = x + 3 + (curCol - hScroll) * cw, cy = y + 2 + (curLine - firstLine) * lh;
    c.fillRect(cx, cy, 2, lh, t.accent);
  }
  if (text.empty() && !focused) gfx::text(c, F::mono, x + 3, y + 3, "...", t.dim);
}

void TextView::set(Canvas& c, const std::string& text, int width, const Font* font) {
  lines = gfx::wrap(c, font, text, width, 400);
  scroll = 0;
}

bool TextView::onKey(const KeyEvent& e, int rows) {
  int maxScroll = (int)lines.size() > rows ? (int)lines.size() - rows : 0;
  int old = scroll;
  switch (navKey(e)) {
    case K_UP: scroll--; break;
    case K_DOWN: scroll++; break;
    case K_LEFT: scroll -= rows; break;
    case K_RIGHT: scroll += rows; break;
    default: return false;
  }
  if (scroll < 0) scroll = 0;
  if (scroll > maxScroll) scroll = maxScroll;
  return old != scroll || true;
}

void TextView::draw(Canvas& c, int x, int y, int w, int h, const Font* font, int lineH, uint16_t color) {
  int rows = h / lineH;
  for (int i = 0; i < rows && scroll + i < (int)lines.size(); i++) gfx::text(c, font, x, y + i * lineH, lines[scroll + i], color);
  if ((int)lines.size() > rows) {
    int th = h * rows / (int)lines.size();
    int ty = y + (h - th) * scroll / ((int)lines.size() - rows);
    c.fillRect(x + w - 2, ty, 2, th < 4 ? 4 : th, T().dim);
  }
}
