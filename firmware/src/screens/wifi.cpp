#include "screens.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

struct Net {
  std::string ssid;
  int rssi = -100;
  bool secure = true, saved = false, inRange = false;
};

class Wifi : public Screen {
 public:
  Wifi() {
    list_.y = STATUS_H + 16 + 11;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 13;
  }

  void onEnter() override {
    svc().net->startScan();
    rebuild();
  }

  // A scan can finish while the splash or a dialog is on top, when update() does not run.
  void onResume() override {
    scanning_ = svc().net->scanning();
    rebuild();
  }

  void update(uint32_t) override {
    NetApi* n = svc().net;
    bool sc = n->scanning();
    if (scanning_ && !sc) rebuild();
    scanning_ = sc;
    WifiState st = n->wifiState();
    if (joining_ && st == WifiState::Connected) {
      joining_ = false;
      ui().toast("done", "Wi-Fi connected", n->ssid());
      if (settings().hosts.empty()) ui().replace(makeHosts());
    } else if (joining_ && st == WifiState::Failed) {
      joining_ = false;
      ui().toast("error", "Could not join", "Check the password and try again.");
    }
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    NetApi* n = svc().net;
    if (e.key == 'r') { n->startScan(); return true; }
    if (e.key == 'h') { hidden(); return true; }
    if (e.key == 'd') { n->disconnectWifi(); return true; }
    if (list_.sel >= (int)nets_.size()) return false;
    Net net = nets_[list_.sel];
    if (e.key == K_ENTER) {
      if (n->wifiState() == WifiState::Connected && n->ssid() == net.ssid) return true;
      for (auto& w : settings().wifi)
        if (w.ssid == net.ssid) return join(w.ssid, w.pass), true;
      if (!net.secure) return join(net.ssid, ""), true;
      ui().input("Password for " + net.ssid, "", false, guard([this, net](const std::string& pass) { join(net.ssid, pass); }));
      return true;
    }
    if (e.key == 'x' && net.saved) {
      settings().forgetWifi(net.ssid);
      settingsSave();
      if (n->ssid() == net.ssid) n->disconnectWifi();
      rebuild();
      return true;
    }
    return false;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    NetApi* n = svc().net;
    WifiState st = n->wifiState();
    gfx::header(c, "Wi-Fi", scanning_ ? "scanning..." : "");
    std::string info;
    uint16_t col = t.dim;
    if (st == WifiState::Connected) {
      info = n->ssid() + "  " + n->ip() + "  " + std::to_string(n->rssi()) + " dBm";
      col = t.ok;
    } else if (st == WifiState::Connecting) info = "Joining " + n->ssid() + "...";
    else if (st == WifiState::Failed) { info = "Join failed"; col = t.err; }
    else info = "Not connected";
    gfx::text(c, F::small, 4, STATUS_H + 18, gfx::ellipsize(c, F::small, info, SCREEN_W - 8), col);
    if (nets_.empty()) {
      if (scanning_) gfx::spinner(c, SCREEN_W / 2, 80, 8, nowMs(), t.accent);
      else gfx::text(c, F::text, SCREEN_W / 2, 76, "No networks found. Press r.", t.dim, gfx::Datum::top_center);
    }
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      const Net& net = nets_[i];
      bool cur = st == WifiState::Connected && n->ssid() == net.ssid;
      gfx::wifi(cv, x + 5, y + 2, net.rssi, net.inRange, cur ? t.ok : t.fg, t.panel2);
      if (net.secure) {
        cv.drawRoundRect(x + 21, y + 3, 5, 4, 2, t.dim);
        cv.fillRect(x + 20, y + 6, 7, 5, t.dim);
      }
      gfx::text(cv, F::text, x + 31, y + 2, gfx::ellipsize(cv, F::text, net.ssid, w - 90), sel ? t.selFg : t.fg);
      if (cur) gfx::chip(cv, x + w - 64, y + 2, "connected", t.bg, t.ok);
      else if (net.saved) gfx::chip(cv, x + w - 40, y + 2, "saved", t.fg, t.panel2);
    });
    gfx::hints(c, {{"ent", "join"}, {"r", "scan"}, {"h", "hidden"}, {"x", "forget"}, {"d", "drop"}});
  }

 private:
  void join(const std::string& ssid, const std::string& pass) {
    settings().rememberWifi(ssid, pass);
    settingsSave();
    svc().net->connectWifi(ssid, pass);
    joining_ = true;
    rebuild();
  }

  void hidden() {
    ui().input("Network name", "", false, guard([this](const std::string& ssid) {
      if (ssid.empty()) return;
      ui().input("Password for " + ssid, "", false, guard([this, ssid](const std::string& pass) { join(ssid, pass); }), "Leave empty for open networks.");
    }));
  }

  void rebuild() {
    std::string cur = list_.sel < (int)nets_.size() ? nets_[list_.sel].ssid : "";
    nets_.clear();
    for (auto& s : svc().net->scanResults()) {
      if (s.ssid.empty()) continue;
      bool dup = false;
      for (auto& n : nets_)
        if (n.ssid == s.ssid) dup = true;
      if (dup) continue;
      Net n;
      n.ssid = s.ssid;
      n.rssi = s.rssi;
      n.secure = s.secure;
      n.inRange = true;
      nets_.push_back(n);
    }
    for (auto& w : settings().wifi) {
      bool found = false;
      for (auto& n : nets_)
        if (n.ssid == w.ssid) n.saved = found = true;
      if (!found) {
        Net n;
        n.ssid = w.ssid;
        n.saved = true;
        nets_.push_back(n);
      }
    }
    list_.count = (int)nets_.size();
    for (int i = 0; i < (int)nets_.size(); i++)
      if (nets_[i].ssid == cur) list_.sel = i;
    list_.clamp();
  }

  ListView list_;
  std::vector<Net> nets_;
  bool scanning_ = false, joining_ = false;
};

}  // namespace

Screen* makeWifi() { return new Wifi(); }
