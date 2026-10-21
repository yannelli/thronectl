#pragma once
#include <cstdint>

enum Key : uint16_t {
  K_NONE = 0,
  K_UP = 0x101, K_DOWN, K_LEFT, K_RIGHT,
  K_ENTER, K_BKSP, K_DEL, K_ESC, K_TAB,
  K_F1 = 0x120, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
};

struct KeyEvent {
  uint16_t key = K_NONE;  // a printable ASCII char, or a Key code
  bool fn = false, shift = false, ctrl = false, alt = false, opt = false;
  bool repeat = false;

  bool isChar() const { return key >= 0x20 && key < 0x7f; }
  char ch() const { return isChar() ? (char)key : 0; }
  bool plain() const { return !fn && !ctrl && !alt && !opt; }
};

/** Navigation meaning of a key: arrows, plus the bare ; . , / ` keys printed with arrows on the Cardputer. */
inline uint16_t navKey(const KeyEvent& e) {
  if (e.key >= K_UP && e.key <= K_TAB) return e.key;
  if (!e.plain()) return K_NONE;
  switch (e.key) {
    case ';': return K_UP;
    case '.': return K_DOWN;
    case ',': return K_LEFT;
    case '/': return K_RIGHT;
    case '`': return K_ESC;
    default: return K_NONE;
  }
}

/** The Esc keycap (` alone) or fn+`. Goes back on every screen, text fields too; Opt+` types a backtick. */
inline bool isEscape(const KeyEvent& e) { return e.key == K_ESC || (e.key == '`' && e.plain()); }

inline bool isBack(const KeyEvent& e) {
  uint16_t n = navKey(e);
  return n == K_ESC || n == K_BKSP;
}
