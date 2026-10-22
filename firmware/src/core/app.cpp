#include "app.h"
#include <cmath>
#include "../screens/screens.h"
#include "settings.h"
#include "store.h"
#include "theme.h"
#include "ui.h"
#include "updater.h"

static App g_app;
App& app() { return g_app; }

static Services g_svc;
Services& svc() { return g_svc; }

void App::begin() {
  Settings& s = settings();
  applyTheme(s.theme, s.accent);
  applyTextSize(s.textSize);
  applySound();
  if (s.tz > 0) svc().sys->setTimezone(TZ_POSIX[s.tz]);
  lastActivity_ = nowMs();
  applyDisplay();
  ui().push(makeHome());
  if (s.wifi.empty()) ui().push(makeWifi());
  else if (s.hosts.empty()) ui().push(makeHosts());
  if (s.splash) ui().push(makeSplash());
  if (s.startupChime) svc().audio->chime(CH_STARTUP);
  svc().net->connectSaved();
}

void App::applyDisplay() {
  Settings& s = settings();
  svc().sys->setRotationFlipped(s.flip);
  static const uint8_t DIM_DIV[] = {12, 6, 3, 2};
  uint8_t b = s.brightness;
  if (lowBatt_ && b > 96) b = 96;
  // The LED is powered through the backlight switch. To keep it lit, the panel sleeps black
  // with the backlight full on behind it.
  if (screenOff_) b = ledKeptLit() ? 255 : 0;
  else if (dimmed_) b = b / DIM_DIV[s.dimLevel % 4] < 4 ? 4 : b / DIM_DIV[s.dimLevel % 4];
  svc().sys->setPanelSleep(screenOff_);
  svc().sys->setBrightness(b);
}

void App::applySound() {
  const Settings& s = settings();
  svc().audio->setVolume(s.muted ? 0 : s.volume);
  svc().audio->setMix(s.alertVol, s.voiceVol);
}

bool App::ledKeptLit() const {
  const Settings& s = settings();
  return s.ledScreenOff && s.ledMode && !lowBatt_;
}

bool App::quietNow() const {
  const Settings& s = settings();
  if (s.dnd) return true;
  int q = s.quietHours % 5, h, m, sec;
  if (!q || !svc().sys->localTime(h, m, sec)) return false;
  int from = QUIET_FROM[q], to = QUIET_TO[q];
  return from > to ? (h >= from || h < to) : (h >= from && h < to);
}

bool App::alertWanted(const std::string& kind, int secs) const {
  const Settings& s = settings();
  if (kind == "attn") return s.notifyAttn;
  if (kind == "error") return s.notifyErr;
  if (kind != "done") return true;
  int d = s.notifyDone % 4;
  return d != 3 && (secs < 0 || secs >= NOTIFY_DONE_SECS[d]);
}

/** Opt+- and Opt+= change the volume; Opt+[ and Opt+] change the brightness. */
void App::nudge(int dVolume, int dBrightness) {
  Settings& s = settings();
  char b[32];
  if (dVolume) {
    int v = s.volume + dVolume;
    s.volume = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    s.muted = false;
    applySound();
    svc().audio->chime(CH_INFO);
    snprintf(b, sizeof b, "Volume %d%%", (s.volume * 100 + 127) / 255);
  } else {
    int v = s.brightness + dBrightness;
    s.brightness = (uint8_t)(v < 8 ? 8 : v > 255 ? 255 : v);
    applyDisplay();
    snprintf(b, sizeof b, "Brightness %d%%", (s.brightness * 100 + 127) / 255);
  }
  settingsSave();
  ui().toast("info", b, "", 900);
}

void App::remindWaiting(uint32_t now) {
  uint32_t every = REMIND_MS[settings().remindWaiting % 4];
  if (!every || !store().countStatus("waiting") || !settings().notifyAttn) { remindAt_ = now; return; }
  if (elapsedMs(now, remindAt_) < every) return;
  remindAt_ = now;
  if (quietNow()) return;
  svc().audio->chime(CH_ATTN);
  ledAlert(LED_ATTN);
}

void App::activity() {
  lastActivity_ = nowMs();
  if (dimmed_ || screenOff_) {
    dimmed_ = screenOff_ = false;
    applyDisplay();
  }
}

void App::onKey(const KeyEvent& e) {
  if (alertSlot_ >= 0 && !alertLen_) alertSlot_ = -1;  // "until a key press" alerts end here
  bool wasOff = screenOff_;
  activity();
  if (wasOff) return;
  if (settings().keyClick) svc().audio->click();
  if (voice.onKey(e)) return;
  Screen* top = ui().top();
  if (e.key == ' ' && e.plain() && settings().spaceTalk && top && !top->typing() && !top->isModal()) return voice.toggle();
  if (e.opt && e.key == K_ENTER) return openLauncher();
  if (e.opt && e.isChar()) {
    switch (e.ch()) {
      case 'h': ui().home(); return;
      case 'a': ui().home(); ui().push(makeAgents()); return;
      case 'y': ui().home(); if (!settings().simpleMode) ui().push(makeApprovals()); return;
      case 'n': ui().push(makeNewAgent("")); return;
      case 't': ui().push(makeTerminal("", "shell")); return;
      case 's': ui().push(makeSettings()); return;
      case 'm': settings().muted = !settings().muted; applySound(); settingsSave();
        ui().toast("info", settings().muted ? "Muted" : "Sound on", "", 1200); return;
      case 'd': settings().dnd = !settings().dnd; settingsSave();
        ui().toast("info", settings().dnd ? "Do not disturb on" : "Do not disturb off", settings().dnd ? "Alerts stay silent." : "", 1500); return;
      case '-': nudge(-16, 0); return;
      case '=': nudge(16, 0); return;
      case '[': nudge(0, -24); return;
      case ']': nudge(0, 24); return;
      case 'k': ui().push(makeKeyHelp()); return;
      default: break;
    }
  }
  ui().handleKey(e);
}

void App::onPtt(bool down) {
  bool wasOff = screenOff_;
  activity();
  if (wasOff && down) return;
  if (down) voice.pttDown();
  else voice.pttUp();
}

void App::connectDefault() {
  manualOffline_ = false;
  backoff_ = 2000;
  if (HostEntry* h = settings().defaultHostEntry()) svc().link->connectHost(*h);
}

int App::reconnectIn(uint32_t now) const {
  Settings& s = settings();
  LinkState st = svc().link->state();
  bool idle = st == LinkState::Offline || st == LinkState::Failed;
  if (!idle || !s.autoConnect || manualOffline_ || !s.defaultHostEntry() || svc().net->wifiState() != WifiState::Connected) return -1;
  return (int32_t)(nextReconnect_ - now) <= 0 ? 0 : (int)((nextReconnect_ - now + 999) / 1000);
}

void App::manualDisconnect() {
  manualOffline_ = true;
  svc().link->disconnect();
}

void App::resubscribe() {
  JsonDocument p;
  JsonArray topics = p["topics"].to<JsonArray>();
  statsMs_ = statsInterval();
  if (statsMs_) topics.add("stats");
  topics.add("agents");
  topics.add("notify");
  if (statsMs_) p["ms"] = statsMs_;
  svc().link->call("sub", p, [](bool, JsonVariantConst, const char*) {});
}

uint32_t App::statsInterval() const {
  const Settings& s = settings();
  uint32_t active = STATS_MS[s.statsRate % 3];
  uint32_t dimmed = s.statsDim % 4 ? STATS_DIM_MS[s.statsDim % 4] : active;
  uint32_t ms = active;
  Screen* base = ui().base();
  bool dash = base && base->wantsStats();
  if (display() == Display::Dimmed || (!dash && dimmed > active)) ms = dimmed;
  else if (display() == Display::Off) ms = s.statsOff % 4 == 3 ? 0 : s.statsOff % 4 ? STATS_OFF_MS[s.statsOff % 4] : dimmed;
  if (lowBatt_ && ms && ms < 10000) ms = 10000;
  return ms;
}

uint32_t App::loopDelayMs() const {
  if (voice.active() || svc().audio->speaking() || updater().active()) return 1;
  return screenOff_ ? 20 : dimmed_ ? 5 : 1;
}

/** Applies CPU speed, Wi-Fi power save and the stats rate when the display state or a setting changes. */
void App::updatePower(uint32_t now) {
  const Settings& s = settings();
  Display d = display();
  bool busy = voice.active() || updater().active();
  uint32_t key = (uint32_t)d | s.cpuMhz << 2 | s.cpuSave << 4 | s.wifiSave << 6 | s.statsRate << 8 | s.statsDim << 10 |
                 s.statsOff << 12 | (lowBatt_ ? 1u : 0u) << 14 | (busy ? 1u : 0u) << 15 | 1u << 16;
  Screen* base = ui().base();
  if (base && base->wantsStats()) key |= 1u << 17;
  if (key == powerKey_) return;
  powerKey_ = key;
  bool slow = lowBatt_ || (s.cpuSave == 2 && d != Display::Active) || (s.cpuSave == 1 && d == Display::Off);
  svc().sys->setCpuMhz(slow && !busy ? 80 : CPU_VALUES[s.cpuMhz % 3]);
  uint8_t ps = s.wifiSave % 4 == 3 ? (d == Display::Off ? 2 : 1) : s.wifiSave % 4;
  if (lowBatt_ && ps < 2) ps = 2;
  svc().net->setPowerSave(busy ? 0 : ps);
  if (svc().link->ready() && statsInterval() != statsMs_) resubscribe();
}

void App::checkBattery(uint32_t now) {
  if (battAt_ && elapsedMs(now, battAt_) < 5000) return;
  battAt_ = now;
  const Settings& s = settings();
  SystemApi* sys = svc().sys;
  int pct = sys->batteryPct();
  bool plugged = sys->charging() || pct < 0;
  bool low = s.lowBattSave && !plugged && pct <= (lowBatt_ ? 25 : 20);
  if (low != lowBatt_) {
    lowBatt_ = low;
    applyDisplay();
    if (low) ui().toast("attn", "Battery saver on", std::to_string(pct) + "% left. CPU, Wi-Fi and stats slow down.");
  }
  if (!plugged && pct <= 10 && !battWarned_) {
    battWarned_ = true;
    svc().audio->chime(CH_ATTN);
    ui().toast("attn", "Battery low", std::to_string(pct) + "% left. Plug in soon.", 6000);
  } else if (plugged || pct > 15) battWarned_ = false;
  if (s.battAutoOff && !plugged && pct <= 3) {
    if (!shutdownAt_) {
      shutdownAt_ = (now + 15000) | 1;
      activity();
      svc().audio->chime(CH_FAIL);
      ui().toast("error", "Battery empty", "Turning off in 15 s. Plug in to stay on.", 15000);
    }
  } else shutdownAt_ = 0;
  if (shutdownAt_ && (int32_t)(now - shutdownAt_) >= 0) {
    settingsSave();
    sys->powerOff();
  }
}

void App::onLinkState(LinkState st) {
  Services& s = svc();
  if (st == LinkState::Ready) {
    backoff_ = 2000;
    manualOffline_ = false;
    const HostInfo& h = s.link->host();
    if (settings().tz == 0 && h.hasTz) {
      char tz[40];
      int off = -h.tzOffMin;
      snprintf(tz, sizeof tz, "<%+03d%02d>%d:%02d", h.tzOffMin / 60, abs(h.tzOffMin) % 60, off / 60, abs(off) % 60);
      s.sys->setTimezone(tz);
    }
    resubscribe();
    refreshHarness(true);
    refreshProjects(true);
    ui().toast("done", "Connected", h.name + (h.paseo ? "  (paseo)" : ""), 2500);
    s.audio->chime(CH_OK);
  } else if (st == LinkState::Pairing) {
    activity();
    if (!pairingVisible()) ui().push(makePairing());
    s.audio->chime(CH_ATTN);
  } else if (lastState_ == LinkState::Ready) {
    store().clearHost();
    ui().toast("error", "Disconnected", s.link->error());
    s.audio->chime(CH_FAIL);
  }
  lastState_ = st;
}

void App::refreshHarness(bool force, std::function<void()> done) {
  if (!force && !store().harness.empty() && nowMs() - store().harnessAt < 60000) {
    if (done) done();
    return;
  }
  svc().link->call("harness.list", [done](bool ok, JsonVariantConst r, const char*) {
    if (ok) store().applyHarness(r);
    if (done) done();
  });
}

void App::refreshProjects(bool force, std::function<void()> done) {
  if (!force && !store().projects.empty() && nowMs() - store().projectsAt < 60000) {
    if (done) done();
    return;
  }
  svc().link->call("projects.list", [done](bool ok, JsonVariantConst r, const char*) {
    if (ok) store().applyProjects(r);
    if (done) done();
  });
}

void App::speakAgent(const std::string& id) {
  JsonDocument p;
  p["id"] = id.c_str();
  p["voice"] = settings().ttsVoice.c_str();
  svc().link->call("agent.speak", p, [](bool ok, JsonVariantConst, const char* err) {
    if (!ok) ui().toast("error", "Read aloud failed", err ? err : "");
  }, 60000);
}

void App::handleNotify(JsonVariantConst d) {
  Settings& s = settings();
  Notice n;
  n.kind = d["kind"] | "info";
  n.title = d["title"] | "";
  n.body = d["body"] | "";
  n.agent = d["agent"] | "";
  n.at = nowMs();
  store().addNotice(n);
  // A filtered alert still lands in Notifications, with no toast, sound or wake.
  if (!alertWanted(n.kind, d["secs"] | -1)) return;
  bool attn = n.kind == "attn" || n.kind == "error", quiet = quietNow();
  if (n.kind == "attn") remindAt_ = n.at;
  if (s.wakeOnNotify && !quiet && (attn || n.kind == "done")) activity();
  ui().toast(n.kind, n.title, n.body);
  ledAlert(n.kind == "done" ? LED_DONE : n.kind == "attn" ? LED_ATTN : n.kind == "error" ? LED_ERROR : LED_INFO);
  if (quiet) return;
  if (s.notifySound == 0 || (s.notifySound == 1 && attn)) {
    svc().audio->chime(n.kind == "done" ? CH_DONE : n.kind == "attn" ? CH_ATTN : n.kind == "error" ? CH_ERROR : CH_INFO);
  }
  if (!n.agent.empty() && !voice.active()) {
    if (n.kind == "done" && s.speakReplies >= 1) speakAgent(n.agent);
    else if (n.kind == "attn" && s.speakReplies == 2) {
      JsonDocument p;
      p["text"] = (n.title + " needs your approval.").c_str();
      p["voice"] = settings().ttsVoice.c_str();
      svc().link->call("tts.say", p, [](bool, JsonVariantConst, const char*) {});
    }
  }
}

void App::onLinkEvent(const char* ev, JsonVariantConst d) {
  if (!strcmp(ev, "stats")) store().applyStats(d);
  else if (!strcmp(ev, "agents")) store().applyAgents(d);
  else if (!strcmp(ev, "notify")) handleNotify(d);
  else if (!strcmp(ev, "ota.offer")) offerUpdate(d);
  else if (!strcmp(ev, "term") || !strcmp(ev, "term_exit")) {
    termAt_ = nowMs();
    if (termSink) termSink(ev, d);
  } else if (!strcmp(ev, "tts")) {
    const char* state = d["state"] | "";
    if (!strcmp(state, "start")) {
      if (!voice.active()) svc().audio->speakerBegin(d["rate"] | 16000);
    } else svc().audio->speakerEnd();
  }
}

void App::onSpeaker(const uint8_t* pcm, size_t bytes) {
  if (!voice.active()) svc().audio->speakerFeed(pcm, bytes);
}

void App::onOtaData(const uint8_t* data, size_t len) { updater().onData(data, len); }

void App::offerUpdate(JsonVariantConst d) {
  OtaApi* ota = svc().ota;
  if (!ota || updater().active()) return Updater::reply(false, "an update is already running on the device", "");
  bool toSd = ota->underLauncher();
  if (toSd && !ota->sdReady()) {
    ui().toast("error", "Update needs an SD card", "Launcher installs apps from the card.");
    return Updater::reply(false, "no SD card in the device (Launcher installs apps from SD)", "sd");
  }
  std::string ver = d["ver"] | "", sha = d["sha256"] | "", from = d["from"] | "the host";
  uint32_t size = d["size"] | 0u;
  std::string what = (ver.empty() ? std::string("New firmware") : "Version " + ver) + " from " + from + ". ";
  activity();
  auto start = [ver, size, sha, toSd](bool yes) {
    if (!yes) return Updater::reply(false, "declined on the device", toSd ? "sd" : "flash");
    updater().startLink(ver, size, sha, toSd);
    ui().push(makeUpdate());
  };
  if (d["asked"] | false) return start(true);  // asked for on this device, which already confirmed it
  svc().audio->chime(CH_ATTN);
  ui().confirm("Update firmware?", what + (toSd ? "Saves to SD for Launcher to install." : "Installs, then restarts."), start);
}

void App::ledAlert(int slot, bool force) {
  if (slot < LED_FIRST_ALERT || slot >= LED_SLOTS || (!force && !settings().ledMode)) return;
  // Error outranks attention, which outranks done; info never cuts in on a running alert.
  static const int RANK[] = {1, 2, 3, 0};
  if (alertSlot_ >= 0 && RANK[alertSlot_ - LED_FIRST_ALERT] > RANK[slot - LED_FIRST_ALERT]) return;
  alertSlot_ = slot;
  alertAt_ = nowMs();
  alertLen_ = led::lengthMs(settings().leds[slot]);
}

void App::ledPreview(int slot) {
  previewSlot_ = slot;
  previewAt_ = nowMs();
}

int App::ledStateSlot() const {
  const Settings& s = settings();
  auto on = [&s](int slot) { return s.leds[slot].fx != 0; };
  bool charging = svc().sys->charging() && on(LED_CHARGING);
  if (!svc().link->ready()) return on(LED_OFFLINE) ? LED_OFFLINE : charging ? LED_CHARGING : -1;
  if (store().countStatus("waiting")) return LED_WAITING;
  if (store().countStatus("working")) return LED_WORKING;
  return charging ? LED_CHARGING : LED_STANDBY;
}

void App::updateLed(uint32_t now) {
  if (elapsedMs(now, ledAt_) < 16) return;  // about 60 updates a second is smooth enough
  ledAt_ = now;
  Settings& s = settings();
  int slot = -1;
  uint32_t t = now;
  uint8_t bright = s.ledBright;
  if (previewSlot_ >= 0) {
    slot = previewSlot_;
    t = elapsedMs(now, previewAt_);
    // Alerts replay with a short gap so their length shows; states just loop.
    uint32_t len = slot >= LED_FIRST_ALERT ? led::lengthMs(s.leds[slot]) : 0;
    if (len && t % (len + 700) >= len) slot = -1;
    else if (len) t %= len + 700;
  } else if (s.ledMode) {
    if (alertSlot_ >= 0 && alertLen_ && elapsedMs(now, alertAt_) >= alertLen_) alertSlot_ = -1;
    if (alertSlot_ >= 0) {
      slot = alertSlot_;
      t = elapsedMs(now, alertAt_);
    } else if (s.ledMode == 1) {
      slot = ledStateSlot();
    }
    if (quietNow()) bright = s.ledQuiet == 0 ? bright : s.ledQuiet == 1 ? bright / 4 : 0;
    if (screenOff_ || lowBatt_) bright /= 2;
    if (screenOff_ && !ledKeptLit()) slot = -1;
  }
  svc().sys->setLed(slot >= 0 ? led::render(s.leds[slot], slot, t, bright) : Rgb{});
}

uint32_t App::frameInterval(uint32_t now) const {
  if (elapsedMs(now, lastActivity_) < 3000 || elapsedMs(now, termAt_) < 1000 || voice.active() || ui().overlay) return 33;
  if (updater().active()) return 80;
  LinkState st = svc().link->state();
  bool busy = st == LinkState::Connecting || st == LinkState::Handshake || st == LinkState::Pairing;
  if (busy || store().countStatus("working") || store().countStatus("waiting")) return 80;
  return 250;
}

void App::loop(uint32_t now) {
  Settings& s = settings();
  Services& sv = svc();
  LinkState st = sv.link->state();
  if (st != lastState_) onLinkState(st);
  bool idle = st == LinkState::Offline || st == LinkState::Failed;
  if (idle && s.autoConnect && !manualOffline_ && sv.net->wifiState() == WifiState::Connected && now >= nextReconnect_) {
    if (HostEntry* h = s.defaultHostEntry()) {
      sv.link->connectHost(*h);
      nextReconnect_ = now + backoff_;
      backoff_ = backoff_ * 2 > 30000 ? 30000 : backoff_ * 2;
    }
  }
  voice.update(now);
  updater().update(now);
  if (updater().phase() != updatePhase_) {
    updatePhase_ = updater().phase();
    if (updatePhase_ == UpdatePhase::Failed) ui().toast("error", "Update failed", updater().error());
    else if (updatePhase_ == UpdatePhase::Done && updater().toSd()) ui().toast("done", "Update saved to SD", "Install it from Launcher.");
  }
  if (updater().restartDue(now)) {
    settingsSave();
    sv.sys->reboot();
  }
  uint32_t quiet = elapsedMs(now, lastActivity_);
  uint32_t dimMs = DIM_MS[s.dimAfter % 6], offMs = OFF_MS[s.offAfter % 6];
  bool watching = s.termAwake && !dimmed_ && !screenOff_ && termSink && elapsedMs(now, termAt_) < 2000;
  if (voice.active() || sv.audio->speaking() || updater().active() || watching) lastActivity_ = now;
  else if (!screenOff_ && offMs && quiet > offMs) {
    screenOff_ = true;
    applyDisplay();
  } else if (!dimmed_ && !screenOff_ && dimMs && quiet > dimMs) {
    dimmed_ = true;
    applyDisplay();
  }
  checkBattery(now);
  remindWaiting(now);
  updatePower(now);
  updateLed(now);
}
