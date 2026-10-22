#include "screens.h"
#include <cmath>
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/store.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

void crown(Canvas& c, int cx, int by, int w, uint16_t col, uint16_t gem) {
  int h = w * 2 / 3, l = cx - w / 2, r = cx + w / 2, top = by - h;
  c.fillRect(l, by - h / 4, w, h / 4, col);
  c.fillTriangle(l, by - h / 4, l, top + h / 5, l + w / 3, by - h / 4, col);
  c.fillTriangle(l + w / 6, by - h / 4, cx, top, r - w / 6, by - h / 4, col);
  c.fillTriangle(r, by - h / 4, r, top + h / 5, r - w / 3, by - h / 4, col);
  c.fillCircle(l, top + h / 5, w / 14, col);
  c.fillCircle(cx, top, w / 12, col);
  c.fillCircle(r, top + h / 5, w / 14, col);
  c.fillCircle(cx, by - h / 8, w / 16, gem);
  c.fillCircle(l + w / 5, by - h / 8, w / 24, gem);
  c.fillCircle(r - w / 5, by - h / 8, w / 24, gem);
}

class Splash : public Screen {
 public:
  void onEnter() override { t0_ = nowMs(); }
  bool statusBar() const override { return false; }
  bool onKey(const KeyEvent&) override { ui().pop(); return true; }
  void update(uint32_t now) override {
    if (elapsedMs(now, t0_) > (settings().animations ? 1700u : 600u)) ui().pop();
  }
  void draw(Canvas& c) override {
    const Theme& t = T();
    uint32_t e = elapsedMs(nowMs(), t0_);
    float k = settings().animations ? (e > 700 ? 1.0f : e / 700.0f) : 1.0f;
    float ease = 1 - (1 - k) * (1 - k) * (1 - k);
    c.fillScreen(t.bg);
    for (int i = 0; i < 5; i++) {
      int r = (int)(20 + i * 18 * ease);
      c.drawCircle(SCREEN_W / 2, 52, r, blend(t.bg, t.accent, (uint8_t)(60 - i * 11)));
    }
    crown(c, SCREEN_W / 2, (int)(70 - 10 * ease), 54, blend(t.bg, t.accent, (uint8_t)(255 * ease)), t.accent2);
    gfx::text(c, F::huge, SCREEN_W / 2, 80, "throne", blend(t.bg, t.fg, (uint8_t)(255 * ease)), gfx::Datum::top_center);
    gfx::text(c, F::small, SCREEN_W / 2, 108, "agents in your pocket", t.dim, gfx::Datum::top_center);
    gfx::text(c, F::small, SCREEN_W - 3, SCREEN_H - 9, "v" FW_VERSION, t.dim, gfx::Datum::top_right);
  }

 private:
  uint32_t t0_ = 0;
};

class Notices : public Screen {
 public:
  Notices() {
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 24;
  }
  void onEnter() override { store().unread = 0; }
  bool onKey(const KeyEvent& e) override {
    auto& ns = store().notices;
    list_.count = (int)ns.size();
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == 'c') { ns.clear(); list_.count = 0; return true; }
    if (e.key == K_ENTER && list_.sel < (int)ns.size()) {
      const Notice& n = ns[list_.sel];
      if (!n.agent.empty() && store().agent(n.agent)) ui().push(makeAgent(n.agent));
      else ui().push(makeTextView(n.title, n.body));
      return true;
    }
    return false;
  }
  void draw(Canvas& c) override {
    const Theme& t = T();
    auto& ns = store().notices;
    store().unread = 0;
    list_.count = (int)ns.size();
    gfx::header(c, "Notifications", std::to_string(ns.size()));
    if (ns.empty()) gfx::emptyState(c, "All quiet", "Agent alerts land here: finished turns, approvals, errors.");
    uint32_t now = nowMs();
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      const Notice& n = ns[i];
      uint16_t col = n.kind == "attn" ? t.warn : n.kind == "error" ? t.err : n.kind == "done" ? t.ok : t.accent;
      cv.fillRoundRect(x + 5, y + 3, 4, 18, 2, col);
      std::string age = fmtAge(elapsedMs(now, n.at) / 1000);
      gfx::text(cv, F::small, x + w - 3, y + 3, age, t.dim, gfx::Datum::top_right);
      gfx::text(cv, F::text, x + 14, y + 1, gfx::ellipsize(cv, F::text, n.title, w - 44), sel ? t.selFg : t.fg);
      gfx::text(cv, F::small, x + 14, y + gfx::height(cv, F::text) + 3, gfx::ellipsize(cv, F::small, n.body, w - 18), t.dim);
    });
    gfx::hints(c, {{"ent", "open"}, {"c", "clear"}, {"`", "back"}});
  }

 private:
  ListView list_;
};

class TextScreen : public Screen {
 public:
  TextScreen(std::string title, std::string text) : title_(std::move(title)), text_(std::move(text)) {}
  bool onKey(const KeyEvent& e) override {
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == 'v' && svc().link->ready()) {
      JsonDocument p;
      p["text"] = text_.c_str();
      p["voice"] = settings().ttsVoice.c_str();
      svc().link->call("tts.say", p, [](bool, JsonVariantConst, const char*) {}, 60000);
      return true;
    }
    if (e.key == 'x') { svc().link->call("tts.stop", [](bool, JsonVariantConst, const char*) {}); svc().audio->speakerStop(); return true; }
    view_.onKey(e, rows_);
    return true;
  }
  void draw(Canvas& c) override {
    if (!wrapped_) {
      view_.set(c, text_, SCREEN_W - 12, F::text);
      wrapped_ = true;
    }
    gfx::header(c, title_, std::to_string(view_.lines.size()) + " lines");
    int top = STATUS_H + HEADER_H + 3;
    rows_ = (SCREEN_H - HINT_H - top) / LINE_H;
    view_.draw(c, 5, top, SCREEN_W - 8, rows_ * LINE_H, F::text, LINE_H, T().fg);
    gfx::hints(c, {{"; .", "scroll"}, {", /", "page"}, {"v", "read aloud"}, {"x", "hush"}, {"`", "back"}});
  }

 private:
  int rows_ = 8;
  std::string title_, text_;
  TextView view_;
  bool wrapped_ = false;
};

class About : public Screen {
 public:
  bool onKey(const KeyEvent& e) override {
    if (isBack(e)) { ui().pop(); return true; }
    uint16_t n = navKey(e);
    if (n == K_DOWN) scroll_++;
    else if (n == K_UP && scroll_ > 0) scroll_--;
    return true;
  }
  void draw(Canvas& c) override {
    const Theme& t = T();
    Services& s = svc();
    std::vector<std::pair<std::string, std::string>> rows;
    char b[64];
    rows.push_back({"Firmware", "thronectl " FW_VERSION});
    rows.push_back({"Installed", s.ota && s.ota->underLauncher() ? "under Launcher (updates via SD)" : "standalone"});
    rows.push_back({"Device ID", s.sys->deviceId()});
    rows.push_back({"Chip", s.sys->chipInfo()});
    snprintf(b, sizeof b, "%u KB free, %u KB low", (unsigned)(s.sys->freeHeap() / 1024), (unsigned)(s.sys->minFreeHeap() / 1024));
    rows.push_back({"Memory", b});
    rows.push_back({"Settings", s.sys->storageInfo()});
    rows.push_back({"Uptime", fmtAge(s.sys->uptimeS())});
    snprintf(b, sizeof b, "%d%%  %.2f V%s", s.sys->batteryPct(), s.sys->batteryVolts(), s.sys->charging() ? "  charging" : "");
    rows.push_back({"Battery", b});
    rows.push_back({"Wi-Fi", s.net->wifiState() == WifiState::Connected ? s.net->ssid() + " " + std::to_string(s.net->rssi()) + " dBm" : "off"});
    rows.push_back({"IP", s.net->ip()});
    rows.push_back({"MAC", s.net->mac()});
    if (s.link->ready()) {
      const HostInfo& h = s.link->host();
      rows.push_back({"Host", h.name});
      rows.push_back({"Hostname", h.hostname});
      rows.push_back({"System", h.os + " " + h.arch});
      rows.push_back({"User", h.user});
      rows.push_back({"Daemon", h.ver + (h.paseo ? "  paseo" : "")});
      rows.push_back({"Latency", std::to_string(s.link->rttMs()) + " ms"});
    } else rows.push_back({"Host", "not connected"});
    gfx::header(c, "About");
    const int top = STATUS_H + HEADER_H + 2, lh = gfx::height(c, F::small) + 2;
    const int vis = (SCREEN_H - HINT_H - top) / lh, vx = gfx::width(c, F::small, "Device ID") + 12;
    int maxScroll = (int)rows.size() - vis;
    if (scroll_ > maxScroll) scroll_ = maxScroll < 0 ? 0 : maxScroll;
    for (int i = 0; i < vis && scroll_ + i < (int)rows.size(); i++) {
      auto& r = rows[scroll_ + i];
      int y = top + i * lh;
      gfx::text(c, F::small, 5, y, r.first, t.dim);
      gfx::text(c, F::small, vx, y, gfx::ellipsize(c, F::small, r.second, SCREEN_W - vx - 4), t.fg);
    }
    crown(c, SCREEN_W - 20, STATUS_H + 34, 22, blend(t.bg, t.accent, 90), blend(t.bg, t.accent2, 90));
    gfx::hints(c, {{"; .", "scroll"}, {"`", "back"}});
  }

 private:
  int scroll_ = 0;
};

}  // namespace

Screen* makeSplash() { return new Splash(); }
Screen* makeNotices() { return new Notices(); }
Screen* makeTextView(const std::string& title, const std::string& text) {
  constexpr size_t MAX_TEXT = 12000;  // about 30 screens; the wrapped copy doubles it
  return new TextScreen(title, text.size() > MAX_TEXT ? text.substr(0, MAX_TEXT) + "\n[cut]" : text);
}
Screen* makeAbout() { return new About(); }

Screen* makeKeyHelp() {
  return makeTextView("Shortcuts",
                      "Everywhere\n"
                      "` or del  back, from any screen\n"
                      "; .  up and down    , /  left and right\n"
                      "[ ]  page up and down in lists\n"
                      "space  talk (again to send), or hold G0\n"
                      "Opt+h  home    Opt+a  agents    Opt+y  approve\n"
                      "Opt+n  new agent    Opt+t  shell\n"
                      "Opt+s  settings    Opt+enter  go to\n"
                      "Opt+m  mute    Opt+d  do not disturb\n"
                      "Opt+- Opt+=  volume\n"
                      "Opt+[ Opt+]  brightness\n"
                      "Opt+k  this list\n"
                      "\n"
                      "Home\n"
                      ", / or 1 to 6  dashboard slides\n"
                      "y  approvals (Settings > Agents > Simple mode\n"
                      "   makes it the home screen)\n"
                      "enter  open the slide's screen\n"
                      "a agents  n new  t shell  p procs\n"
                      "h hosts  w Wi-Fi  s settings  l alerts\n"
                      "m menu  c connect\n"
                      "\n"
                      "Agents\n"
                      "y allow  d deny  enter open  t terminal\n"
                      "p prompt  f filter  k end  1 to 9 open\n"
                      "[ ]  agent before or after, in an agent\n"
                      "\n"
                      "Approvals\n"
                      "y or enter allow  d deny  c comment\n"
                      ", /  next card   Y allow all   o open\n"
                      "\n"
                      "Terminal\n"
                      "`  leave (press twice for a shell)\n"
                      "fn+`  send Esc    Opt+q  leave\n"
                      "Opt+`  type a backtick");
}
