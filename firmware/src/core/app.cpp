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
  applySound();
  svc().sys->setCpuMhz(CPU_VALUES[s.cpuMhz % 3]);
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
  uint8_t b = s.brightness;
  if (screenOff_) b = 0;
  else if (dimmed_) b = b / 6 < 4 ? 4 : b / 6;
  svc().sys->setBrightness(b);
}

void App::applySound() {
  svc().audio->setVolume(settings().volume);
}

void App::activity() {
  lastActivity_ = nowMs();
  if (dimmed_ || screenOff_) {
    dimmed_ = screenOff_ = false;
    applyDisplay();
  }
}

void App::onKey(const KeyEvent& e) {
  bool wasOff = screenOff_;
  activity();
  if (wasOff) return;
  if (settings().keyClick) svc().audio->click();
  if (voice.onKey(e)) return;
  if (e.opt && e.key == K_ENTER) return openLauncher();
  if (e.opt && e.isChar()) {
    switch (e.ch()) {
      case 'h': ui().home(); return;
      case 'a': ui().home(); ui().push(makeAgents()); return;
      case 'n': ui().push(makeNewAgent("")); return;
      case 't': ui().push(makeTerminal("", "shell")); return;
      case 's': ui().push(makeSettings()); return;
      case 'm': settings().volume = settings().volume ? 0 : 140; applySound(); settingsSave();
        ui().toast("info", settings().volume ? "Sound on" : "Muted"); return;
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

void App::manualDisconnect() {
  manualOffline_ = true;
  svc().link->disconnect();
}

void App::resubscribe() {
  JsonDocument p;
  JsonArray topics = p["topics"].to<JsonArray>();
  topics.add("stats");
  topics.add("agents");
  topics.add("notify");
  p["ms"] = STATS_MS[settings().statsRate % 3];
  svc().link->call("sub", p, [](bool, JsonVariantConst, const char*) {});
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
  bool attn = n.kind == "attn" || n.kind == "error";
  if (s.wakeOnNotify && (attn || n.kind == "done")) activity();
  ui().toast(n.kind, n.title, n.body);
  if (s.notifySound == 0 || (s.notifySound == 1 && attn)) {
    svc().audio->chime(n.kind == "done" ? CH_DONE : n.kind == "attn" ? CH_ATTN : n.kind == "error" ? CH_ERROR : CH_INFO);
  }
  const Theme& t = T();
  ledFlashColor_ = n.kind == "done" ? t.ok : attn ? t.warn : t.accent;
  ledFlashUntil_ = nowMs() + 3000;
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
  svc().audio->chime(CH_ATTN);
  ui().confirm("Update firmware?", what + (toSd ? "Saves to SD for Launcher to install." : "Installs, then restarts."),
               [ver, size, sha, toSd](bool yes) {
                 if (!yes) return Updater::reply(false, "declined on the device", toSd ? "sd" : "flash");
                 updater().startLink(ver, size, sha, toSd);
                 ui().push(makeUpdate());
               });
}

void App::updateLed(uint32_t now) {
  Settings& s = settings();
  const Theme& t = T();
  uint16_t color = 0;
  uint8_t level = 0;
  if (now < ledFlashUntil_ && s.ledMode > 0) {
    color = ledFlashColor_;
    level = ((now / 250) % 2) ? s.ledBright : 0;
  } else if (s.ledMode == 1) {
    if (store().countStatus("waiting")) {
      color = t.warn;
      level = ((now / 600) % 2) ? s.ledBright : s.ledBright / 6;
    } else if (store().countStatus("working")) {
      color = t.accent;
      level = (uint8_t)(s.ledBright * (0.25f + 0.75f * (0.5f + 0.5f * sinf(now / 500.0f))));
    }
  }
  svc().sys->setLed(color, screenOff_ && level ? level / 2 : level);
}

uint32_t App::frameInterval(uint32_t now) const {
  if (elapsedMs(now, lastActivity_) < 3000 || elapsedMs(now, termAt_) < 1000 || voice.active() || ui().overlay) return 33;
  if (updater().active()) return 80;
  LinkState st = svc().link->state();
  bool busy = st == LinkState::Connecting || st == LinkState::Handshake || st == LinkState::Pairing;
  if (busy || store().countStatus("working") || store().countStatus("waiting") || now < ledFlashUntil_) return 80;
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
  if (updater().restartDue(now)) {
    settingsSave();
    sv.sys->reboot();
  }
  uint32_t quiet = elapsedMs(now, lastActivity_);
  uint32_t dimMs = DIM_MS[s.dimAfter % 6], offMs = OFF_MS[s.offAfter % 6];
  if (voice.active() || sv.audio->speaking() || updater().active()) lastActivity_ = now;
  else if (!screenOff_ && offMs && quiet > offMs) {
    screenOff_ = true;
    applyDisplay();
  } else if (!dimmed_ && !screenOff_ && dimMs && quiet > dimMs) {
    dimmed_ = true;
    applyDisplay();
  }
  updateLed(now);
}
