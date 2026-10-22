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
  std::function<std::string()> value;  // optional text beside an action's arrow
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

Item pick(const char* label, uint8_t& ref, const char* const* names, int n) {
  Item i{Item::CHOICE, label};
  i.get = [&ref, n] { return (int)(ref % n); };
  i.set = [&ref](int v) { ref = (uint8_t)v; };
  i.options = [names, n] { return std::vector<std::string>(names, names + n); };
  return i;
}

Screen* makeLedSlot(int slot);

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

/** Applies a whole new Settings and rebuilds the stack, since sizes and themes may change. */
void adopt(const Settings& next, int sel) {
  settings() = next;
  settingsSave();
  Settings& s = settings();
  applyTheme(s.theme, s.accent);
  applyTextSize(s.textSize);
  if (s.tz) svc().sys->setTimezone(TZ_POSIX[s.tz]);
  app().applySound();
  app().applyDisplay();
  ui().home();
  ui().push(makeSettings());
  ui().push(makeSettingsCategory(8, sel));
}

void askHostUpdate(const std::string& fw, const std::string& host) {
  ui().confirm("Firmware is up to date", "v" + fw + " is the newest. Update thronectl on the host (v" + host + ") from git? Its daemon restarts.",
               [](bool yes) {
                 if (!yes) return;
                 svc().link->call("update.host", [](bool ok, JsonVariantConst, const char* err) {
                   if (!ok) return ui().toast("error", "Host update failed", err ? err : "");
                   ui().toast("info", "Host is updating", "The device reconnects when the daemon is back.", 6000);
                 });
               });
}

/** Asks the host for newer firmware, or offers to update the host itself when the firmware is current. */
void checkUpdates() {
  if (!svc().link->ready()) return ui().toast("error", "Not connected", "Connect to a host first.");
  ui().toast("info", "Checking for updates", "The host looks at the latest release.", 3000);
  JsonDocument p;
  p["force"] = true;
  svc().link->call("update.check", p, [](bool ok, JsonVariantConst r, const char* err) {
    if (!ok) return ui().toast("error", "Update check failed", err ? err : "");
    std::string latest = r["latest"] | "", source = r["source"] | "", fw = r["fw"] | FW_VERSION, host = r["host"] | "";
    if (latest.empty() && !(r["hostUpdate"] | false))
      return ui().toast("info", "No firmware on the host", r["note"] | "It needs gh for releases, or a local build.", 6000);
    if (!(r["newer"] | false)) {
      if (r["hostUpdate"] | false) return askHostUpdate(fw, host);
      return ui().toast("done", "Up to date", "Firmware v" + fw + ", host v" + host + ".");
    }
    ui().confirm("Update firmware?", "Version " + latest + " from " + source + ". Installs, then restarts.", [](bool yes) {
      if (!yes) return;
      JsonDocument none;
      none.to<JsonObject>();
      svc().link->call("update.firmware", none, [](bool ok, JsonVariantConst, const char* err) {
        if (!ok) ui().toast("error", "Update failed", err ? err : "");
      }, 90000);
    });
  }, 90000);
}

/** Back to defaults, keeping what it takes to reconnect: Wi-Fi, paired hosts, name and time zone. */
void resetKeepPairing() {
  const Settings& cur = settings();
  Settings next;
  next.wifi = cur.wifi;
  next.hosts = cur.hosts;
  next.defaultHost = cur.defaultHost;
  next.deviceName = cur.deviceName;
  next.tz = cur.tz;
  adopt(next, 5);
  ui().toast("done", "Settings reset", "Wi-Fi and pairing kept.");
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
      v.push_back(choice("Text size", s.textSize, TEXT_SIZE_OPTS, [] {
        applyTextSize(settings().textSize);
        settingsSave();
        // Screens size their lists when built, so rebuild the stack at the new size.
        ui().home();
        ui().push(makeSettings());
        ui().push(makeSettingsCategory(0, 2));
      }));
      v.push_back(slider("Brightness", s.brightness, 8, 255, 16, display));
      v.push_back(choice("Dim after", s.dimAfter, DIM_OPTS));
      v.push_back(choice("Dimmed brightness", s.dimLevel, DIM_LEVEL_OPTS));
      v.push_back(choice("Screen off after", s.offAfter, OFF_OPTS));
      v.push_back(toggle("Flip screen", s.flip, display));
      v.push_back(choice("Terminal font", s.termFont, TERM_FONT_OPTS));
      v.push_back(toggle("Terminal output keeps screen on", s.termAwake));
      v.push_back(toggle("24-hour clock", s.clock24));
      v.push_back(toggle("Battery percent", s.showBattPct));
      v.push_back(toggle("Animations", s.animations));
      v.push_back(toggle("Boot splash", s.splash));
      v.push_back(choice("Rotate dashboard", s.dashCycle, DASH_CYCLE_OPTS));
      v.push_back(slider("Toast seconds", s.toastSecs, 2, 10, 1));
      break;
    }
    case 1:
      v.push_back(slider("Volume", s.volume, 0, 255, 16, [] { settings().muted = false; app().applySound(); svc().audio->chime(CH_INFO); }));
      v.push_back(slider("Alert volume", s.alertVol, 0, 255, 16, [] { app().applySound(); svc().audio->chime(CH_DONE); }));
      v.push_back(slider("Voice volume", s.voiceVol, 0, 255, 16, [] { app().applySound(); }));
      v.push_back(choice("Key clicks", s.keyClick, CLICK_OPTS));
      v.push_back(choice("Alert sounds", s.notifySound, NOTIFY_OPTS));
      v.push_back(toggle("Do not disturb", s.dnd));
      v.push_back(choice("Quiet hours", s.quietHours, QUIET_OPTS));
      v.push_back(choice("Finished turns", s.notifyDone, NOTIFY_DONE_OPTS));
      v.push_back(toggle("Approval alerts", s.notifyAttn));
      v.push_back(toggle("Error alerts", s.notifyErr));
      v.push_back(choice("Remind while waiting", s.remindWaiting, REMIND_OPTS));
      v.push_back(toggle("Wake on alerts", s.wakeOnNotify));
      v.push_back(toggle("Startup chime", s.startupChime));
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
      v.push_back(toggle("Space to talk", s.spaceTalk));
      v.push_back(choice("Mic gain", s.micGain, MIC_GAIN_OPTS));
      v.push_back(toggle("Send without review", s.autoSend));
      v.push_back(text("Voice name", s.ttsVoice, "Host TTS voice, e.g. Samantha, alloy or af_sky"));
      v.push_back(action("Test speaker", [] { say("Throne is listening. Your agents are in good hands."); }));
      v.push_back(action("Host speech", [] { ui().push(makeHostConfig("voice", "Host speech")); }, "local models, providers"));
      break;
    case 3:
      v.push_back(choice("LED", s.ledMode, LED_OPTS));
      v.push_back(slider("Brightness", s.ledBright, 0, 255, 16));
      v.push_back(choice("In quiet hours", s.ledQuiet, LED_QUIET_OPTS));
      v.push_back(choice("With screen off", s.ledScreenOff, LED_SCREEN_OFF_OPTS));
      for (int i = 0; i < LED_SLOTS; i++) {
        Item it = action(led::SLOT_NAMES[i], [i] { ui().push(makeLedSlot(i)); });
        it.value = [&s, i] { return std::string(led::FX_NAMES[s.leds[i].fx % led::FX_COUNT]); };
        v.push_back(it);
      }
      v.push_back(action("Reset LED effects", [] {
        danger("Reset LED effects?", "Every state and alert goes back to its default.", [] { settings().leds = led::allDefaults(); });
      }));
      break;
    case 4: {
      v.push_back(toggle("Simple mode", s.simpleMode));
      v.push_back(toggle("Confirm before ending", s.confirmKill));
      v.push_back(toggle("Open terminal on launch", s.attachAfterCreate));
      v.push_back(toggle("Show exited agents", s.showExited));
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
      v.push_back(action("Host settings", [] { ui().push(makeHostConfig("", "Host settings")); }, "edit the daemon"));
      v.push_back(toggle("Auto-connect", s.autoConnect));
      v.push_back(text("Device name", s.deviceName, "Shown on the host when pairing"));
      v.push_back(choice("Time zone", s.tz, TZ_NAMES, [&s] { if (s.tz) svc().sys->setTimezone(TZ_POSIX[s.tz]); }));
      v.push_back(action("Disconnect", [] { app().manualDisconnect(); ui().toast("info", "Offline"); }));
      v.push_back(action("Reconnect", [] { app().connectDefault(); }));
      break;
    case 6:
      v.push_back(choice("Key repeat delay", s.repeatDelay, REPEAT_DELAY_OPTS));
      v.push_back(choice("Key repeat rate", s.repeatRate, REPEAT_RATE_OPTS));
      v.push_back(action("Shortcuts", [] { ui().push(makeKeyHelp()); }));
      break;
    case 7:
      // App::updatePower applies these on its next loop, so no callbacks are needed.
      v.push_back(choice("CPU speed", s.cpuMhz, CPU_OPTS));
      v.push_back(choice("CPU saver", s.cpuSave, CPU_SAVE_OPTS));
      v.push_back(choice("Wi-Fi power save", s.wifiSave, WIFI_SAVE_OPTS));
      v.push_back(choice("Stats when active", s.statsRate, STATS_OPTS));
      v.push_back(choice("Stats when dimmed", s.statsDim, STATS_DIM_OPTS));
      v.push_back(choice("Stats when screen off", s.statsOff, STATS_OFF_OPTS));
      v.push_back(toggle("Battery saver at 20%", s.lowBattSave));
      v.push_back(toggle("Turn off at 3%", s.battAutoOff));
      v.push_back(action("Power off", [] { danger("Power off?", "Press G0 to turn it back on. The side switch turns it fully off.", [] { settingsSave(); svc().sys->powerOff(); }); }));
      break;
    default:
      v.push_back(action("About this device", [] { ui().push(makeAbout()); }));
      v.push_back(action("Check for updates", checkUpdates, "ask the host"));
      v.push_back(action("Update from SD card", [] { ui().push(makeSdUpdate()); }));
      v.push_back(action("Back up settings to SD", [] { ui().push(makeSdBrowser(true)); }, "pick a folder"));
      v.push_back(action("Restore settings from SD", [] { ui().push(makeSdBrowser(false)); }, "pick a backup"));
      v.push_back(action("Reset settings", [] { danger("Reset settings?", "Back to defaults. Wi-Fi and paired hosts stay.", resetKeepPairing); }));
      v.push_back(action("Reboot", [] { danger("Reboot?", "Settings are saved first.", [] { settingsSave(); svc().sys->reboot(); }); }));
      v.push_back(action("Factory reset", [] {
        danger("Factory reset?", "Erases Wi-Fi, paired hosts, keys and all settings.", [] { svc().sys->factoryReset(); });
      }));
      break;
  }
  return v;
}

const char* const CATS[] = {"Display", "Sound", "Voice", "LED", "Agents", "Network", "Keyboard", "Power", "Device"};
const char* const CAT_SUB[] = {"theme, text size, sleep", "volume, alerts, quiet", "talk, read aloud, mic",
                               "effects per state", "simple mode, launch", "Wi-Fi, hosts, time zone",
                               "repeat, shortcuts", "CPU, Wi-Fi, battery", "about, update, reset"};
constexpr int CAT_COUNT = 9;

class Category : public Screen {
 public:
  Category(int idx, int sel) : Category(CATS[idx], category(idx), sel) {}
  /** `preview` is an LED slot to show on the LED while this page is open, or -1. */
  Category(std::string title, std::vector<Item> items, int sel = 0, int preview = -1)
      : title_(std::move(title)), items_(std::move(items)), preview_(preview) {
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 14;
    list_.count = (int)items_.size();
    list_.sel = sel;
  }
  ~Category() override {
    if (preview_ >= 0) app().ledPreview(-1);
  }

  void onEnter() override {
    if (preview_ >= 0) app().ledPreview(preview_);
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
    gfx::header(c, title_.c_str());
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      Item& it = items_[i];
      gfx::text(cv, F::text, x + 6, y + 2, it.label, sel ? t.selFg : t.fg);
      int rx = x + w - 5, room = w - 19 - gfx::width(cv, F::text, it.label);
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
          gfx::text(cv, F::text, rx, y + 2, gfx::ellipsize(cv, F::text, s, room), t.accent, gfx::Datum::top_right);
          break;
        }
        case Item::TEXT:
          gfx::text(cv, F::text, rx, y + 2, gfx::ellipsize(cv, F::text, it.str->empty() ? "(default)" : *it.str, room),
                    it.str->empty() ? t.dim : t.accent, gfx::Datum::top_right);
          break;
        case Item::ACTION:
          cv.drawLine(rx - 4, y + 3, rx, y + 7, t.dim);
          cv.drawLine(rx, y + 7, rx - 4, y + 11, t.dim);
          if (it.value) gfx::text(cv, F::text, rx - 9, y + 2, gfx::ellipsize(cv, F::text, it.value(), room - 9), t.accent, gfx::Datum::top_right);
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

  std::string title_;
  std::vector<Item> items_;
  int preview_;
  ListView list_;
};

Screen* makeLedSlot(int slot) {
  LedSlot& l = settings().leds[slot];
  std::vector<Item> v;
  v.push_back(pick("Effect", l.fx, led::FX_NAMES, led::FX_COUNT));
  v.push_back(pick("Color", l.color, led::COLOR_NAMES, led::COLOR_COUNT));
  v.push_back(pick("Speed", l.speed, led::SPEED_NAMES, led::SPEED_COUNT));
  v.push_back(pick("Level", l.level, led::LEVEL_NAMES, led::LEVEL_COUNT));
  if (slot >= LED_FIRST_ALERT) v.push_back(pick("Length", l.length, led::LENGTH_NAMES, led::LENGTH_COUNT));
  v.push_back(action("Default", [slot] { settings().leds[slot] = led::defaults(slot); }, "reset this one"));
  return new Category(std::string("LED: ") + led::SLOT_NAMES[slot], std::move(v), 0, slot);
}

class SettingsMenu : public Screen {
 public:
  SettingsMenu() {
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 12;
    list_.count = CAT_COUNT;
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == K_ENTER || navKey(e) == K_RIGHT) { ui().push(new Category(list_.sel, 0)); return true; }
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

void restoreSettingsFile(const std::string& path) {
  std::string json;
  if (!svc().ota->sdReadFile(path, json)) return ui().toast("error", "Could not read the backup", svc().ota->error());
  Settings next = settings();
  if (!settingsFromJson(next, json)) return ui().toast("error", "Backup is damaged", "The file is not valid settings JSON.");
  adopt(next, 4);
  ui().toast("done", "Settings restored", path);
}

Screen* makeSettings() { return new SettingsMenu(); }
Screen* makeSettingsCategory(int idx, int sel) { return new Category(idx, sel); }
