#include "screens.h"
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

std::function<void()> g_retry;
bool g_pairingOpen = false;

void connectTo(std::function<void()> go) {
  g_retry = go;
  go();
  if (!g_pairingOpen) ui().push(makePairing());
}

struct Row {
  enum Kind { SAVED, FOUND, ADD } kind;
  int idx;
};

class Hosts : public Screen {
 public:
  Hosts() {
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 22;
  }

  void onEnter() override { scan(); }
  void onResume() override { rebuild(); }

  void update(uint32_t now) override {
    size_t n = svc().net->beacons().size();
    if (n != seen_) rebuild();
    if (!svc().net->discovering() && elapsedMs(now, scanned_) > 8000) scan();
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == 'r') { scan(); return true; }
    if (e.key == 'a') { addManual(); return true; }
    if (e.key == 'o') { app().manualDisconnect(); ui().toast("info", "Disconnected"); return true; }
    if (list_.sel >= (int)rows_.size()) return false;
    Row row = rows_[list_.sel];
    Settings& s = settings();
    if (e.key == K_ENTER) {
      if (row.kind == Row::ADD) addManual();
      else if (row.kind == Row::SAVED) {
        HostEntry h = s.hosts[row.idx];
        connectTo([h] { svc().link->connectHost(h); });
      } else {
        Beacon b = svc().net->beacons()[row.idx];
        connectTo([b] { svc().link->connectNew(b.ip, b.port, b.name); });
      }
      return true;
    }
    if (row.kind != Row::SAVED) return false;
    HostEntry h = s.hosts[row.idx];
    if (e.key == 'd') {
      s.defaultHost = h.id;
      settingsSave();
      ui().toast("done", "Default host", h.name);
      return true;
    }
    if (e.key == 'x') {
      ui().confirm("Forget host?", h.name + ". You will need to pair again.", guard([this, h](bool yes) {
        if (!yes) return;
        if (svc().link->ready() && svc().link->host().id == h.id) app().manualDisconnect();
        settings().removeHost(h.id);
        settingsSave();
        rebuild();
      }));
      return true;
    }
    return false;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    bool busy = svc().net->discovering();
    gfx::header(c, "Hosts", busy ? "searching..." : "");
    if (busy) gfx::spinner(c, SCREEN_W - 70, STATUS_H + 8, 4, nowMs(), t.accent);
    if (svc().net->wifiState() != WifiState::Connected) {
      gfx::emptyState(c, "No Wi-Fi", "Join a network first (Home, then w).");
      gfx::hints(c, {{"`", "back"}});
      return;
    }
    Settings& s = settings();
    LinkApi* link = svc().link;
    uint32_t now = nowMs();
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      const Row& r = rows_[i];
      if (r.kind == Row::ADD) {
        gfx::text(cv, F::text, x + 8, y + 6, "+ Add by address...", t.accent);
        return;
      }
      std::string name, addr, os;
      bool saved = r.kind == Row::SAVED;
      if (saved) {
        const HostEntry& h = s.hosts[r.idx];
        name = h.name; addr = h.addr + ":" + std::to_string(h.port); os = h.os;
        bool live = link->ready() && link->host().id == h.id;
        if (live) cv.fillCircle(x + 8, y + 7, 3, t.ok);
        else cv.drawCircle(x + 8, y + 7, 3, t.dim);
        if (h.id == s.defaultHostEntry()->id) gfx::text(cv, F::small, x + 5, y + gfx::height(cv, F::text) + 2, "*", t.accent);
      } else {
        const Beacon& b = svc().net->beacons()[r.idx];
        name = b.name; addr = b.ip + ":" + std::to_string(b.port); os = b.os;
        gfx::spinner(cv, x + 8, y + 7, 3, now / 3, t.accent2);
      }
      std::string tag = saved ? (os.empty() ? "paired" : os) : "new";
      int tw = gfx::width(cv, F::small, tag) + 6;
      gfx::chip(cv, x + w - tw - 3, y + 2, tag, saved ? t.fg : t.bg, saved ? t.panel2 : t.accent2);
      gfx::text(cv, F::text, x + 16, y + 1, gfx::ellipsize(cv, F::text, name, w - tw - 24), sel ? t.selFg : t.fg);
      gfx::text(cv, F::small, x + 16, y + gfx::height(cv, F::text) + 2, addr, t.dim);
    });
    if (rows_.size() == 1) {
      gfx::text(c, F::small, SCREEN_W / 2, 74, "Run  thronectl daemon  on your computer", t.dim, gfx::Datum::top_center);
      gfx::text(c, F::small, SCREEN_W / 2, 84, "on the same network.", t.dim, gfx::Datum::top_center);
    }
    gfx::hints(c, {{"ent", "connect"}, {"d", "default"}, {"x", "forget"}, {"a", "add"}, {"r", "scan"}, {"o", "offline"}});
  }

 private:
  void scan() {
    scanned_ = nowMs();
    if (svc().net->wifiState() == WifiState::Connected) svc().net->startDiscovery();
    rebuild();
  }

  void rebuild() {
    Settings& s = settings();
    rows_.clear();
    for (int i = 0; i < (int)s.hosts.size(); i++) rows_.push_back({Row::SAVED, i});
    auto& bs = svc().net->beacons();
    seen_ = bs.size();
    for (int i = 0; i < (int)bs.size(); i++)
      if (!s.findHost(bs[i].id)) rows_.push_back({Row::FOUND, i});
    rows_.push_back({Row::ADD, 0});
    list_.count = (int)rows_.size();
    list_.clamp();
  }

  void addManual() {
    ui().input("Host address", "", false, [](const std::string& v) {
      if (v.empty()) return;
      std::string addr = v;
      uint16_t port = THRONE_PORT;
      size_t colon = v.rfind(':');
      if (colon != std::string::npos) {
        addr = v.substr(0, colon);
        port = (uint16_t)atoi(v.c_str() + colon + 1);
        if (!port) port = THRONE_PORT;
      }
      connectTo([addr, port] { svc().link->connectNew(addr, port, addr); });
    }, "IP or hostname, optional :port");
  }

  ListView list_;
  std::vector<Row> rows_;
  size_t seen_ = 0;
  uint32_t scanned_ = 0;
};

class Pairing : public Screen {
 public:
  Pairing() { g_pairingOpen = true; }
  ~Pairing() override { g_pairingOpen = false; }

  void update(uint32_t now) override {
    LinkState s = svc().link->state();
    if (s == LinkState::Ready && !doneAt_) doneAt_ = now;
    if (doneAt_ && elapsedMs(now, doneAt_) > 1400) {
      doneAt_ = 0;
      ui().home();
    }
  }

  bool onKey(const KeyEvent& e) override {
    LinkState s = svc().link->state();
    if (isBack(e)) {
      if (s != LinkState::Ready && s != LinkState::Failed) app().manualDisconnect();
      ui().pop();
      return true;
    }
    if (e.key == 'r' && (s == LinkState::Failed || s == LinkState::Offline)) {
      if (g_retry) g_retry();
      else app().connectDefault();
      return true;
    }
    return true;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    LinkApi* link = svc().link;
    LinkState s = link->state();
    uint32_t now = nowMs();
    std::string name = s == LinkState::Ready ? link->host().name : link->targetName();
    gfx::header(c, "Pair", gfx::ellipsize(c, F::small, name, 140));
    int cx = SCREEN_W / 2, o = STATUS_H + HEADER_H - 28;
    if (s == LinkState::Pairing) {
      std::string code = link->pairCode();
      if (code.size() == 6) code = code.substr(0, 3) + " " + code.substr(3);
      gfx::text(c, F::text, cx, o + 33, "Confirm this code on the host", t.dim, gfx::Datum::top_center);
      int w = gfx::width(c, F::huge, code) + 24;
      c.fillRoundRect(cx - w / 2, o + 47, w, 34, 6, t.panel);
      c.drawRoundRect(cx - w / 2, o + 47, w, 34, 6, blend(t.panel, t.accent, (uint8_t)(140 + 100 * ((now / 700) % 2))));
      gfx::text(c, F::huge, cx, o + 52, code, t.accent, gfx::Datum::top_center);
      gfx::text(c, F::small, cx, o + 88, "run:  thronectl pair", t.fg, gfx::Datum::top_center);
      gfx::text(c, F::small, cx, o + 99, "and answer y if the codes match", t.dim, gfx::Datum::top_center);
    } else if (s == LinkState::Ready) {
      c.fillCircle(cx, 62, 16, t.ok);
      c.drawLine(cx - 7, 62, cx - 2, 68, t.bg);
      c.drawLine(cx - 2, 68, cx + 8, 56, t.bg);
      c.drawLine(cx - 7, 63, cx - 2, 69, t.bg);
      c.drawLine(cx - 2, 69, cx + 8, 57, t.bg);
      gfx::text(c, F::bold, cx, 85, "Connected", t.fg, gfx::Datum::top_center);
    } else if (s == LinkState::Failed || s == LinkState::Offline) {
      c.drawCircle(cx, 58, 14, t.err);
      c.drawLine(cx - 6, 52, cx + 6, 64, t.err);
      c.drawLine(cx + 6, 52, cx - 6, 64, t.err);
      std::string err = link->error().empty() ? "Disconnected" : link->error();
      auto lines = gfx::wrap(c, F::text, err, SCREEN_W - 20, 3);
      for (size_t i = 0; i < lines.size(); i++) gfx::text(c, F::text, cx, 78 + (int)i * LINE_H, lines[i], t.fg, gfx::Datum::top_center);
    } else {
      gfx::spinner(c, cx, 60, 12, now, t.accent);
      gfx::text(c, F::text, cx, 80, s == LinkState::Connecting ? "Connecting..." : "Securing link...", t.fg, gfx::Datum::top_center);
    }
    if (s == LinkState::Failed || s == LinkState::Offline) gfx::hints(c, {{"r", "retry"}, {"`", "back"}});
    else gfx::hints(c, {{"`", s == LinkState::Ready ? "back" : "cancel"}});
  }

 private:
  uint32_t doneAt_ = 0;
};

}  // namespace

Screen* makeHosts() { return new Hosts(); }
Screen* makePairing() { return new Pairing(); }
bool pairingVisible() { return g_pairingOpen; }
