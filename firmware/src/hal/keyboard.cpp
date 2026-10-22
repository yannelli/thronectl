#include <M5Cardputer.h>
#include <algorithm>
#include "../core/settings.h"
#include "hal.h"

// Positions in the M5Cardputer key map (x = column, y = row).
static bool isMod(int x, int y) { return (y == 2 && x <= 1) || (y == 3 && x <= 2); }

bool KeyboardHal::makeEvent(Pos p, KeyEvent& e) const {
  KeyValue_t v = M5Cardputer.Keyboard.getKeyValue({p.x, p.y});
  uint8_t first = (uint8_t)v.value_first;
  e = KeyEvent();
  e.fn = fn_;
  e.shift = shift_;
  e.ctrl = ctrl_;
  e.opt = opt_;
  e.alt = alt_;
  if (first == KEY_BACKSPACE) e.key = fn_ ? K_DEL : K_BKSP;
  else if (first == KEY_TAB) e.key = K_TAB;
  else if (first == KEY_ENTER) e.key = K_ENTER;
  else if (fn_) {
    switch (first) {
      case ';': e.key = K_UP; break;
      case '.': e.key = K_DOWN; break;
      case ',': e.key = K_LEFT; break;
      case '/': e.key = K_RIGHT; break;
      case '`': e.key = K_ESC; break;
      case '-': e.key = K_F11; break;
      case '=': e.key = K_F12; break;
      default:
        if (first >= '1' && first <= '9') e.key = K_F1 + (first - '1');
        else if (first == '0') e.key = K_F10;
        else e.key = first;
    }
    if (e.key >= K_UP) e.fn = false;
  } else {
    e.key = (shift_ && !ctrl_) ? (uint8_t)v.value_second : first;
  }
  return e.key != K_NONE;
}

void KeyboardHal::poll(uint32_t now, const std::function<void(const KeyEvent&)>& emit) {
  // M5Cardputer.update() reads one TCA8418 event, but the chip queues up to 10. Read the rest
  // one at a time, so a fast press and release both reach the diff in scan().
  scan(now, emit);
  for (int i = 0; i < 9; i++) {
    auto before = M5Cardputer.Keyboard.keyList();
    M5Cardputer.Keyboard.updateKeyList();
    if (M5Cardputer.Keyboard.keyList() == before) break;
    scan(now, emit);
  }
}

void KeyboardHal::scan(uint32_t now, const std::function<void(const KeyEvent&)>& emit) {
  const auto& list = M5Cardputer.Keyboard.keyList();
  std::vector<Pos> cur;
  fn_ = shift_ = ctrl_ = opt_ = alt_ = false;
  for (const auto& k : list) {
    Pos p{(int8_t)k.x, (int8_t)k.y};
    cur.push_back(p);
    if (p.y == 2 && p.x == 0) fn_ = true;
    else if (p.y == 2 && p.x == 1) shift_ = true;
    else if (p.y == 3 && p.x == 0) ctrl_ = true;
    else if (p.y == 3 && p.x == 1) opt_ = true;
    else if (p.y == 3 && p.x == 2) alt_ = true;
  }
  for (const Pos& p : cur) {
    if (isMod(p.x, p.y) || std::find(prev_.begin(), prev_.end(), p) != prev_.end()) continue;
    KeyEvent e;
    if (makeEvent(p, e)) emit(e);
    repeatPos_ = p;
    repeatAt_ = now + REPEAT_DELAY_MS[settings().repeatDelay % 3];
  }
  bool held = repeatPos_.x >= 0 && std::find(cur.begin(), cur.end(), repeatPos_) != cur.end();
  if (!held) repeatPos_ = {-1, -1};
  else if ((int32_t)(now - repeatAt_) >= 0) {
    KeyEvent e;
    if (makeEvent(repeatPos_, e)) {
      e.repeat = true;
      emit(e);
    }
    repeatAt_ = now + REPEAT_RATE_MS[settings().repeatRate % 3];
  }
  prev_ = std::move(cur);
}
