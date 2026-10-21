#include "screens.h"
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/store.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

struct Item {
  enum Kind { CHOICE, SLIDER, TOGGLE, TEXT, ACTION } kind;
  std::string label;
  std::function<int()> get;
  std::function<void(int)> set;
  std::function<std::vector<std::string>()> options;
  int lo = 0, hi = 255, step = 16;
  std::string* str = nullptr;
  std::function<void()> run;
  std::string hint;
};

std::vector<std::string> optLabels(const OptList& o) { return std::vector<std::string>(o.labels, o.labels + o.count); }

Item choice(const char* label, uint8_t& ref, const OptList& o, std::function<void()> after = nullptr) {
  Item i{Item::CHOICE, label};
  i.get = [&ref] { return (int)ref; };
  i.set = [&ref, after](int v) { ref = (uint8_t)v; if (after) after(); };
  i.options = [&o] { return optLabels(o); };
  return i;
}

Item slider(const char* label, uint8_t& ref, int lo, int hi, int step, std::function<void()> after = nullptr) {
  Item i{Item::SLIDER, label};
  i.get = [&ref] { return (int)ref; };
  i.set = [&ref, after](int v) { ref = (uint8_t)v; if (after) after(); };
  i.lo = lo; i.hi = hi; i.step = step;
  return i;
}

Item toggle(const char* label, bool& ref, std::function<void()> after = nullptr) {
  Item i{Item::TOGGLE, label};
  i.get = [&ref] { return ref ? 1 : 0; };
  i.set = [&ref, after](int v) { ref = v != 0; if (after) after(); };
  return i;
}

Item text(const char* label, std::string& ref, const char* hint) {
  Item i{Item::TEXT, label};
  i.str = &ref;
  i.hint = hint;
  return i;
}

Item action(const char* label, std::function<void()> fn, const char* hint = "") {
  Item i{Item::ACTION, label};
  i.run = std::move(fn);
  i.hint = hint;
  return i;
}

void retheme() { applyTheme(settings().theme, settings().accent); }
void display() { app().applyDisplay(); }

void say(const std::string& text) {
  if (!svc().link->ready()) return ui().toast("error", "Not connected");
  JsonDocument p;
  p["text"] = text.c_str();
  p["voice"] = settings().ttsVoice.c_str();
  svc().link->call("tts.say", p, [](bool ok, JsonVariantConst, const char* err) {
    if (!ok) ui().toast("error", "Speech failed", err ? err : "");
  }, 30000);
}

void danger(const char* title, const char* msg, std::function<void()> fn) {
  ui().confirm(title, msg, [fn](bool yes) { if (yes) fn(); });
}

std::vector<Item> category(int idx) {
  Settings& s = settings();
  std::vector<Item> v;
  switch (idx) {
    case 0: {
      Item th{Item::CHOICE, "Theme"};
      th.get = [&s] { return (int)s.theme; };
      th.set = [&s](int x) { s.theme = (uint8_t)x; retheme(); };
      th.options = [] { std::vector<std::string> o; for (int i = 0; i < THEME_COUNT; i++) o.push_back(THEMES[i].name); return o; };
      v.push_back(th);
      Item ac{Item::CHOICE, "Accent"};
      ac.get = [&s] { return (int)s.accent; };
      ac.set = [&s](int x) { s.accent = (uint8_t)x; retheme(); };
      ac.options = [] { std::vector<std::string> o; for (int i = 0; i < ACCENT_COUNT; i++) o.push_back(ACCENTS[i].name); return o; };
      v.push_back(ac);
      v.push_back(slider("Brightness", s.brightness, 8, 255, 16, display));
      v.push_back(choice("Dim after", s.dimAfter, DIM_OPTS));
      v.push_back(choice("Screen off after", s.offAfter, OFF_OPTS));
      v.push_back(toggle("Flip screen", s.flip, display));
      v.push_back(choice("Terminal font", s.termFont, TERM_FONT_OPTS));
      v.push_back(toggle("24-hour clock", s.clock24));
      v.push_back(toggle("Battery percent", s.showBattPct));
      v.push_back(toggle("Animations", s.animations));
      v.push_back(toggle("Boot splash", s.splash));
      v.push_back(slider("Toast seconds", s.toastSecs, 2, 10, 1));
      break;
    }
    case 1:
      v.push_back(slider("Volume", s.volume, 0, 255, 16, [] { app().applySound(); svc().audio->chime(CH_INFO); }));
      v.push_back(choice("Key clicks", s.keyClick, CLICK_OPTS));
      v.push_back(choice("Alert sounds", s.notifySound, NOTIFY_OPTS));
      v.push_back(toggle("Startup chime", s.startupChime));
      v.push_back(toggle("Wake on alerts", s.wakeOnNotify));
      v.push_back(action("Play all chimes", [] {
        static uint8_t n = 0;
        static const char* const names[] = {"startup", "done", "attention", "error", "info", "ok", "fail"};
        svc().audio->chime((Chime)n);
        ui().toast("info", std::string("Chime: ") + names[n], "", 1200);
        n = (n + 1) % 7;
      }, "press again for the next one"));
      break;
    case 2:
      v.push_back(choice("Read replies aloud", s.speakReplies, SPEAK_OPTS));
      v.push_back(choice("Push to talk", s.pttMode, PTT_OPTS));
      v.push_back(choice("Mic gain", s.micGain, MIC_GAIN_OPTS));
      v.push_back(toggle("Send without review", s.autoSend));
      v.push_back(text("Voice name", s.ttsVoice, "Host TTS voice, e.g. Samantha or alloy"));
      v.push_back(action("Test speaker", [] { say("Throne is listening. Your agents are in good hands."); }));
      break;
    case 3:
      v.push_back(choice("LED", s.ledMode, LED_OPTS));
      v.push_back(slider("LED brightness", s.ledBright, 0, 255, 16));
      break;
    case 4: {
      v.push_back(toggle("Confirm before ending", s.confirmKill));
      v.push_back(toggle("Open terminal on launch", s.attachAfterCreate));
      v.push_back(toggle("Show exited agents", s.showExited));
      v.push_back(choice("Stats refresh", s.statsRate, STATS_OPTS, [] { if (svc().link->ready()) app().resubscribe(); }));
      Item h{Item::CHOICE, "Default harness"};
      h.get = [&s] {
        auto& hs = store().harness;
        for (int i = 0; i < (int)hs.size(); i++) if (hs[i].id == s.lastHarness) return i;
        return 0;
      };
      h.set = [&s](int x) { if (x < (int)store().harness.size()) s.lastHarness = store().harness[x].id; };
      h.options = [] { std::vector<std::string> o; for (auto& x : store().harness) o.push_back(x.name); return o; };
      v.push_back(h);
      v.push_back(action("Clear notifications", [] { store().notices.clear(); store().unread = 0; ui().toast("done", "Cleared"); }));
      break;
    }
    case 5:
      v.push_back(action("Wi-Fi networks", [] { ui().push(makeWifi()); }));
      v.push_back(action("Hosts and pairing", [] { ui().push(makeHosts()); }));
      v.push_back(toggle("Auto-connect", s.autoConnect));
      v.push_back(text("Device name", s.deviceName, "Shown on the host when pairing"));
      v.push_back(choice("Time zone", s.tz, TZ_NAMES, [&s] { if (s.tz) svc().sys->setTimezone(TZ_POSIX[s.tz]); }));
      v.push_back(action("Disconnect", [] { app().manualDisconnect(); ui().toast("info", "Offline"); }));
      v.push_back(action("Reconnect", [] { app().connectDefault(); }));
      break;
    case 6:
      v.push_back(choice("Key repeat delay", s.repeatDelay, REPEAT_DELAY_OPTS));
      v.push_back(choice("Key repeat rate", s.repeatRate, REPEAT_RATE_OPTS));
      v.push_back(choice("CPU speed", s.cpuMhz, CPU_OPTS, [&s] { svc().sys->setCpuMhz(CPU_VALUES[s.cpuMhz % 3]); }));
      v.push_back(action("Power off", [] { danger("Power off?", "Press the side button to turn it back on.", [] { settingsSave(); svc().sys->powerOff(); }); }));
      break;
    default:
      v.push_back(action("About this device", [] { ui().push(makeAbout()); }));
      v.push_back(action("Reboot", [] { danger("Reboot?", "Settings are saved first.", [] { settingsSave(); svc().sys->reboot(); }); }));
      v.push_back(action("Factory reset", [] {
        danger("Factory reset?", "Erases Wi-Fi, paired hosts, keys and all settings.", [] { svc().sys->factoryReset(); });
      }));
      break;
  }
  return v;
}

const char* const CATS[] = {"Display", "Sound", "Voice", "LED", "Agents", "Network", "Keyboard & power", "Device"};
const char* const CAT_SUB[] = {"theme, brightness, sleep", "volume, clicks, alerts", "talk, read aloud, mic",
                               "status light", "launch, confirm, stats", "Wi-Fi, hosts, time zone",
                               "repeat, CPU, power", "about, reboot, reset"};
constexpr int CAT_COUNT = 8;

class Category : public Screen {
 public:
  explicit Category(int idx) : idx_(idx), items_(category(idx)) {
    list_.y = STATUS_H + 16;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 14;
    list_.count = (int)items_.size();
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) {
      settingsSave();
      ui().pop();
      return true;
    }
    Item& it = items_[list_.sel];
    uint16_t nav = navKey(e);
    if (nav == K_LEFT || nav == K_RIGHT) {
      adjust(it, nav == K_RIGHT ? 1 : -1);
      return true;
    }
    if (e.key != K_ENTER) return false;
    switch (it.kind) {
      case Item::TOGGLE: it.set(!it.get()); break;
      case Item::SLIDER: adjust(it, 1); break;
      case Item::CHOICE: {
        Item* p = &it;
        ui().choose(it.label, it.options(), it.get(), [p](int v) { p->set(v); });
        break;
      }
      case Item::TEXT: {
        std::string* str = it.str;
        ui().input(it.label, *str, false, [str](const std::string& v) { *str = v; settingsSave(); }, it.hint);
        break;
      }
      case Item::ACTION: it.run(); break;
    }
    return true;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    gfx::header(c, CATS[idx_]);
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      Item& it = items_[i];
      gfx::text(cv, F::text, x + 6, y + 2, it.label, sel ? t.selFg : t.fg);
      int rx = x + w - 5;
      switch (it.kind) {
        case Item::TOGGLE: {
          bool on = it.get();
          cv.fillRoundRect(rx - 18, y + 3, 18, 9, 4, on ? t.accent : t.panel2);
          cv.fillCircle(on ? rx - 5 : rx - 14, y + 7, 3, on ? t.bg : t.dim);
          break;
        }
        case Item::SLIDER: {
          int v = it.get();
          float f = (float)(v - it.lo) / (it.hi - it.lo);
          gfx::bar(cv, rx - 56, y + 5, 56, 5, f, t.accent, t.panel2);
          std::string n = std::to_string(it.hi == 255 ? (v * 100 + 127) / 255 : v) + (it.hi == 255 ? "%" : "");
          gfx::text(cv, F::small, rx - 60, y + 3, n, t.dim, gfx::Datum::top_right);
          break;
        }
        case Item::CHOICE: {
          auto o = it.options();
          int v = it.get();
          std::string s = v >= 0 && v < (int)o.size() ? o[v] : "-";
          gfx::text(cv, F::text, rx, y + 2, gfx::ellipsize(cv, F::text, s, 110), t.accent, gfx::Datum::top_right);
          break;
        }
        case Item::TEXT:
          gfx::text(cv, F::text, rx, y + 2, gfx::ellipsize(cv, F::text, it.str->empty() ? "(default)" : *it.str, 100),
                    it.str->empty() ? t.dim : t.accent, gfx::Datum::top_right);
          break;
        case Item::ACTION:
          cv.drawLine(rx - 4, y + 3, rx, y + 7, t.dim);
          cv.drawLine(rx, y + 7, rx - 4, y + 11, t.dim);
          break;
      }
    });
    const Item& it = items_[list_.sel];
    if (!it.hint.empty()) gfx::hints(c, {{"ent", it.hint.c_str()}});
    else if (it.kind == Item::SLIDER || it.kind == Item::CHOICE) gfx::hints(c, {{", /", "change"}, {"ent", "pick"}, {"`", "back"}});
    else gfx::hints(c, {{"ent", "select"}, {"`", "back"}});
  }

 private:
  void adjust(Item& it, int dir) {
    if (it.kind == Item::TOGGLE) it.set(!it.get());
    else if (it.kind == Item::SLIDER) {
      int v = it.get() + dir * it.step;
      if (v > it.hi) v = dir > 0 && it.get() == it.hi ? it.lo : it.hi;
      if (v < it.lo) v = it.lo;
      it.set(v);
    } else if (it.kind == Item::CHOICE) {
      int n = (int)it.options().size();
      if (n) it.set((it.get() + dir + n) % n);
    }
  }

  int idx_;
  std::vector<Item> items_;
  ListView list_;
};

class SettingsMenu : public Screen {
 public:
  SettingsMenu() {
    list_.y = STATUS_H + 16;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 12;
    list_.count = CAT_COUNT;
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == K_ENTER || navKey(e) == K_RIGHT) { ui().push(new Category(list_.sel)); return true; }
    return false;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    gfx::header(c, "Settings", "v" FW_VERSION);
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      gfx::text(cv, F::text, x + 6, y + 1, CATS[i], sel ? t.selFg : t.fg);
      gfx::text(cv, F::small, x + w - 4, y + 2, CAT_SUB[i], sel ? t.accent : t.dim, gfx::Datum::top_right);
    });
    gfx::hints(c, {{"ent", "open"}, {"`", "back"}});
  }

 private:
  ListView list_;
};

}  // namespace

Screen* makeSettings() { return new SettingsMenu(); }
