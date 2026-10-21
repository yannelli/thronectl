#pragma once
#include "platform.h"
#include "input.h"

/** Scrolling list. The caller draws each row. */
class ListView {
 public:
  int sel = 0, first = 0;
  int count = 0;
  int y = 28, h = 97, rowH = 12;

  /** Handles up/down and paging. Returns true when the key moved the selection. */
  bool onKey(const KeyEvent& e);
  void clamp();
  void draw(Canvas& c, const std::function<void(Canvas&, int idx, int x, int y, int w, int h, bool selected)>& row);
  /** rowH plus the text-size padding. */
  int rowHeight() const { return rowH + ROW_PAD; }
  int visible() const { return h / rowHeight(); }
};

/** Single or multi-line text editor with a cursor. */
class TextInput {
 public:
  std::string text;
  size_t cursor = 0;
  bool multiline = false;
  size_t maxLen = 1500;
  bool mask = false;

  /** Consumes printable keys, backspace, delete and fn-arrow cursor moves. */
  bool onKey(const KeyEvent& e);
  void set(const std::string& s);
  void insert(const std::string& s);
  void draw(Canvas& c, int x, int y, int w, int h, bool focused);
};

/** Read-only scrolling text. */
class TextView {
 public:
  std::vector<std::string> lines;
  int scroll = 0;

  void set(Canvas& c, const std::string& text, int width, const Font* font);
  bool onKey(const KeyEvent& e, int visibleRows);
  void draw(Canvas& c, int x, int y, int w, int h, const Font* font, int lineH, uint16_t color);
};
