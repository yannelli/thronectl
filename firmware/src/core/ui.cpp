#include "ui.h"
#include "gfx.h"
#include "services.h"
#include "settings.h"
#include "store.h"
#include "theme.h"
#include "widgets.h"

static UI g_ui;
UI& ui() { return g_ui; }

void UI::push(Screen* s) {
  stack_.push_back(s);
  s->onEnter();
}

void UI::pop() {
  if (stack_.size() <= 1) return;
  graveyard_.push_back(stack_.back());
  stack_.pop_back();
  stack_.back()->onResume();
}

void UI::replace(Screen* s) {
  if (!stack_.empty()) {
    graveyard_.push_back(stack_.back());
    stack_.pop_back();
  }
  push(s);
}

void UI::home() {
  while (stack_.size() > 1) {
    graveyard_.push_back(stack_.back());
    stack_.pop_back();
  }
  if (!stack_.empty()) stack_.back()->onResume();
}

void UI::handleKey(const KeyEvent& e) {
  if (Screen* s = top()) s->onKey(e);
}

void UI::tick(uint32_t now) {
  for (Screen* s : graveyard_) delete s;
  graveyard_.clear();
  if (Screen* s = top()) s->update(now);
  if (toast_.until && now > toast_.until) toast_ = Toast();
}

static void drawToast(Canvas& c, const Toast& t, uint32_t now) {
  const Theme& th = T();
  uint16_t col = t.kind == "attn" ? th.warn : t.kind == "error" ? th.err : t.kind == "done" ? th.ok : th.accent;
  int h = t.body.empty() ? 18 : 28;
  uint32_t age = now - t.start;
  int slide = age < 160 ? (int)((160 - age) * (h + 6) / 160) : 0;
  int y = SCREEN_H - HINT_H - h - 2 + slide;
  c.fillRoundRect(6, y, SCREEN_W - 12, h, 5, th.panel2);
  c.drawRoundRect(6, y, SCREEN_W - 12, h, 5, col);
  c.fillRoundRect(6, y, 5, h, 2, col);
  gfx::text(c, F::text, 16, y + 4, gfx::ellipsize(c, F::text, t.title, SCREEN_W - 30), th.fg);
  if (!t.body.empty()) gfx::text(c, F::small, 16, y + 17, gfx::ellipsize(c, F::small, t.body, SCREEN_W - 30), th.dim);
}

void UI::render(Canvas& c) {
  const Theme& t = T();
  c.fillScreen(t.bg);
  int base = (int)stack_.size() - 1;
  while (base > 0 && stack_[base]->isModal()) base--;
  for (int i = base < 0 ? 0 : base; i < (int)stack_.size(); i++) {
    if (i > base) {
      for (int y = 0; y < SCREEN_H; y += 2) c.drawFastHLine(0, y, SCREEN_W, t.bg);
    }
    stack_[i]->draw(c);
  }
  if (Screen* s = top(); s && s->statusBar()) drawStatusBar(c);
  if (toast_.until) drawToast(c, toast_, nowMs());
  if (overlay) overlay(c);
}

void UI::toast(const std::string& kind, const std::string& title, const std::string& body, uint32_t ms) {
  toast_.kind = kind;
  toast_.title = title;
  toast_.body = body;
  toast_.start = nowMs();
  toast_.until = toast_.start + (ms ? ms : settings().toastSecs * 1000u);
}

namespace {

class ConfirmDialog : public Screen {
 public:
  ConfirmDialog(std::string title, std::string msg, std::function<void(bool)> cb)
      : title_(std::move(title)), msg_(std::move(msg)), cb_(std::move(cb)) {}
  bool isModal() const override { return true; }
  bool onKey(const KeyEvent& e) override {
    uint16_t n = navKey(e);
    if (n == K_LEFT || n == K_RIGHT || n == K_TAB) yes_ = !yes_;
    else if (e.key == 'y') finish(true);
    else if (e.key == 'n' || isBack(e)) finish(false);
    else if (e.key == K_ENTER) finish(yes_);
    return true;
  }
  void draw(Canvas& c) override {
    const Theme& t = T();
    int x = 20, y = 30, w = 200, h = 78;
    c.fillRoundRect(x, y, w, h, 6, t.panel);
    c.drawRoundRect(x, y, w, h, 6, t.accent);
    gfx::text(c, F::bold, x + w / 2, y + 6, gfx::ellipsize(c, F::bold, title_, w - 12), t.fg, gfx::Datum::top_center);
    auto lines = gfx::wrap(c, F::text, msg_, w - 16, 3);
    for (size_t i = 0; i < lines.size(); i++) gfx::text(c, F::text, x + w / 2, y + 24 + (int)i * 11, lines[i], t.dim, gfx::Datum::top_center);
    button(c, x + 30, y + h - 18, "No", !yes_);
    button(c, x + w - 30 - 60, y + h - 18, "Yes", yes_);
  }

 private:
  void button(Canvas& c, int x, int y, const char* label, bool on) {
    const Theme& t = T();
    c.fillRoundRect(x, y, 60, 13, 4, on ? t.accent : t.panel2);
    gfx::text(c, F::text, x + 30, y + 2, label, on ? t.bg : t.fg, gfx::Datum::top_center);
  }
  void finish(bool v) {
    auto cb = cb_;
    ui().pop();
    if (cb) cb(v);
  }
  std::string title_, msg_;
  std::function<void(bool)> cb_;
  bool yes_ = false;
};

class ChoiceDialog : public Screen {
 public:
  ChoiceDialog(std::string title, std::vector<std::string> opts, int sel, std::function<void(int)> cb)
      : title_(std::move(title)), opts_(std::move(opts)), cb_(std::move(cb)) {
    list_.count = (int)opts_.size();
    list_.sel = sel;
    list_.rowH = 12;
    int vis = list_.count < 7 ? list_.count : 7;
    list_.h = vis * 12;
    list_.y = (SCREEN_H - list_.h) / 2 + 7;
  }
  bool isModal() const override { return true; }
  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == K_ENTER) {
      auto cb = cb_;
      int v = list_.sel;
      ui().pop();
      if (cb) cb(v);
    }
    return true;
  }
  void draw(Canvas& c) override {
    const Theme& t = T();
    int x = 24, w = SCREEN_W - 48, y = list_.y - 16;
    c.fillRoundRect(x - 2, y - 2, w + 4, list_.h + 20, 6, t.panel);
    c.drawRoundRect(x - 2, y - 2, w + 4, list_.h + 20, 6, t.accent);
    gfx::text(c, F::text, x + 6, y + 1, title_, t.accent);
    c.setClipRect(x, list_.y, w, list_.h);
    list_.draw(c, [&](Canvas& cv, int i, int, int ry, int, int, bool s) {
      gfx::text(cv, F::text, x + 8, ry + 1, gfx::ellipsize(cv, F::text, opts_[i], w - 14), s ? t.selFg : t.fg);
    });
    c.clearClipRect();
  }

 private:
  std::string title_;
  std::vector<std::string> opts_;
  std::function<void(int)> cb_;
  ListView list_;
};

class InputScreen : public Screen {
 public:
  InputScreen(std::string title, const std::string& initial, bool multiline, std::function<void(const std::string&)> cb, std::string hint)
      : title_(std::move(title)), cb_(std::move(cb)), hint_(std::move(hint)) {
    in_.multiline = multiline;
    in_.set(initial);
  }
  bool onKey(const KeyEvent& e) override {
    if (e.key == K_ESC || (e.key == '`' && e.plain() && in_.text.empty())) { ui().pop(); return true; }
    if (e.key == K_ENTER && !(in_.multiline && e.shift)) {
      auto cb = cb_;
      std::string v = in_.text;
      ui().pop();
      if (cb) cb(v);
      return true;
    }
    in_.onKey(e);
    return true;
  }
  bool onVoiceText(const std::string& text) override {
    in_.insert((in_.text.empty() || in_.text.back() == ' ' ? "" : " ") + text);
    return true;
  }
  std::string voiceTarget() const override { return "dictate"; }
  void draw(Canvas& c) override {
    gfx::header(c, title_);
    int top = STATUS_H + 19;
    int h = in_.multiline ? SCREEN_H - HINT_H - top - (hint_.empty() ? 3 : 13) : 16;
    in_.draw(c, 4, top, SCREEN_W - 8, h, true);
    if (!hint_.empty()) gfx::text(c, F::small, 5, top + h + 3, hint_, T().dim);
    if (in_.multiline) gfx::hints(c, {{"ent", "ok"}, {"sh+ent", "newline"}, {"fn+`", "cancel"}, {"G0", "dictate"}});
    else gfx::hints(c, {{"ent", "ok"}, {"fn+`", "cancel"}});
  }

 private:
  std::string title_;
  std::function<void(const std::string&)> cb_;
  std::string hint_;
  TextInput in_;
};

}  // namespace

void UI::confirm(const std::string& title, const std::string& msg, std::function<void(bool)> cb) {
  push(new ConfirmDialog(title, msg, std::move(cb)));
}

void UI::choose(const std::string& title, const std::vector<std::string>& options, int selected, std::function<void(int)> cb) {
  push(new ChoiceDialog(title, options, selected, std::move(cb)));
}

void UI::input(const std::string& title, const std::string& initial, bool multiline, std::function<void(const std::string&)> cb,
               const std::string& hint) {
  push(new InputScreen(title, initial, multiline, std::move(cb), hint));
}

void drawStatusBar(Canvas& c) {
  const Theme& t = T();
  Services& s = svc();
  c.fillRect(0, 0, SCREEN_W, STATUS_H, t.panel);
  LinkState ls = s.link ? s.link->state() : LinkState::Offline;
  uint16_t dot = ls == LinkState::Ready ? t.ok : ls == LinkState::Failed ? t.err : ls == LinkState::Offline ? t.dim : t.warn;
  c.fillCircle(5, 6, 3, dot);
  std::string name = ls == LinkState::Ready ? s.link->host().name : s.link && !s.link->targetName().empty() ? s.link->targetName() : "offline";
  int x = SCREEN_W - 3;
  if (s.sys) {
    int pct = s.sys->batteryPct();
    x -= 18;
    gfx::battery(c, x, 2, pct, s.sys->charging(), t.ok, t.dim);
    if (settings().showBattPct && pct >= 0) {
      std::string p = std::to_string(pct);
      x -= gfx::width(c, F::small, p) + 2;
      gfx::text(c, F::small, x, 2, p, t.dim);
    }
  }
  if (s.net) {
    x -= 14;
    gfx::wifi(c, x, 2, s.net->rssi(), s.net->wifiState() == WifiState::Connected, t.fg, t.panel2);
  }
  int h, m, sec;
  if (s.sys && s.sys->localTime(h, m, sec)) {
    char buf[12];
    if (settings().clock24) snprintf(buf, sizeof buf, "%02d:%02d", h, m);
    else snprintf(buf, sizeof buf, "%d:%02d%s", (h + 11) % 12 + 1, m, h < 12 ? "a" : "p");
    x -= gfx::width(c, F::small, buf) + 5;
    gfx::text(c, F::small, x, 2, buf, t.fg);
  }
  Store& st = store();
  int waiting = st.countStatus("waiting"), working = st.countStatus("working");
  if (waiting) {
    std::string w = "!" + std::to_string(waiting);
    x -= gfx::width(c, F::small, w) + 6;
    c.fillRoundRect(x - 2, 1, gfx::width(c, F::small, w) + 4, 10, 3, t.warn);
    gfx::text(c, F::small, x, 2, w, t.bg);
  }
  if (working) {
    std::string w = std::to_string(working);
    x -= gfx::width(c, F::small, w) + 14;
    gfx::spinner(c, x + 4, 6, 4, nowMs(), t.accent);
    gfx::text(c, F::small, x + 10, 2, w, t.accent);
  }
  gfx::text(c, F::small, 11, 2, gfx::ellipsize(c, F::small, name, x - 16), ls == LinkState::Ready ? t.fg : t.dim);
}
