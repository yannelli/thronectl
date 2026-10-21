#include "screens.h"
#include <cmath>
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/store.h"
#include "../core/theme.h"

namespace {

const char* stateLabel(LinkState s) {
  switch (s) {
    case LinkState::Connecting: return "Connecting";
    case LinkState::Handshake: return "Securing link";
    case LinkState::Pairing: return "Waiting for pairing";
    case LinkState::Ready: return "Connected";
    case LinkState::Failed: return "Connection failed";
    default: return "Offline";
  }
}

void gauge(Canvas& c, int x, int y, int w, const char* label, float frac, const std::string& value, const std::string& sub,
           const char* caption) {
  const Theme& t = T();
  c.fillRoundRect(x, y, w, 44, 4, t.panel);
  gfx::text(c, F::small, x + 4, y + 3, label, t.dim);
  if (!sub.empty() && x + 8 + gfx::width(c, F::big, value) + gfx::width(c, F::small, sub) < x + w) {
    gfx::text(c, F::small, x + w - 4, y + 14, sub, t.fg, gfx::Datum::top_right);
    gfx::text(c, F::small, x + w - 4, y + 24, caption, t.dim, gfx::Datum::top_right);
  }
  uint16_t col = frac < 0 ? t.dim : gfx::loadColor(frac);
  gfx::text(c, F::big, x + 4, y + 13, value, frac < 0 ? t.dim : t.fg);
  gfx::bar(c, x + 4, y + 36, w - 8, 4, frac < 0 ? 0 : frac, col, t.panel2);
}

std::string pct(float v) {
  char b[8];
  snprintf(b, sizeof b, "%d%%", (int)lroundf(v));
  return b;
}

class Home : public Screen {
 public:
  bool onKey(const KeyEvent& e) override {
    if (!e.plain() && !e.shift) return false;
    switch (e.key) {
      case 'a': case K_ENTER: ui().push(makeAgents()); return true;
      case 'n': ui().push(makeNewAgent("")); return true;
      case 't': ui().push(makeTerminal("", "shell")); return true;
      case 'p': ui().push(makeProcs()); return true;
      case 'h': ui().push(makeHosts()); return true;
      case 'w': ui().push(makeWifi()); return true;
      case 's': ui().push(makeSettings()); return true;
      case 'l': ui().push(makeNotices()); return true;
      case 'm': case K_TAB: openLauncher(); return true;
      case 'c': if (!svc().link->ready()) app().connectDefault(); return true;
      default: return false;
    }
  }

  void draw(Canvas& c) override {
    LinkState ls = svc().link->state();
    if (ls != LinkState::Ready) drawOffline(c, ls);
    else drawDash(c);
    if (ls == LinkState::Ready) gfx::hints(c, {{"a", "agents"}, {"n", "new"}, {"t", "term"}, {"p", "procs"}, {"m", "menu"}, {"G0", "talk"}});
    else gfx::hints(c, {{"h", "hosts"}, {"w", "wifi"}, {"c", "connect"}, {"s", "settings"}, {"m", "menu"}});
  }

 private:
  void drawOffline(Canvas& c, LinkState ls) {
    const Theme& t = T();
    Services& s = svc();
    uint32_t now = nowMs();
    int cy = 50;
    bool busy = ls == LinkState::Connecting || ls == LinkState::Handshake || ls == LinkState::Pairing;
    if (busy) gfx::spinner(c, SCREEN_W / 2, cy, 14, now, t.accent);
    else {
      c.drawCircle(SCREEN_W / 2, cy, 14, ls == LinkState::Failed ? t.err : t.dim);
      c.drawLine(SCREEN_W / 2 - 7, cy + 7, SCREEN_W / 2 + 7, cy - 7, ls == LinkState::Failed ? t.err : t.dim);
    }
    gfx::text(c, F::bold, SCREEN_W / 2, cy + 20, stateLabel(ls), t.fg, gfx::Datum::top_center);
    std::string sub;
    if (s.net->wifiState() != WifiState::Connected) sub = s.net->wifiState() == WifiState::Connecting ? "Joining Wi-Fi..." : "Wi-Fi is off. Press w.";
    else if (settings().hosts.empty()) sub = "No paired host. Press h to find one.";
    else if (!s.link->targetName().empty()) sub = s.link->targetName();
    if (ls == LinkState::Failed && !s.link->error().empty()) sub = s.link->error();
    gfx::text(c, F::text, SCREEN_W / 2, cy + 36, gfx::ellipsize(c, F::text, sub, SCREEN_W - 16), t.dim, gfx::Datum::top_center);
  }

  void drawDash(Canvas& c) {
    const Theme& t = T();
    Store& st = store();
    const Stats& x = st.stats;
    const HostInfo& h = svc().link->host();
    gfx::text(c, F::bold, 4, 14, gfx::ellipsize(c, F::bold, h.name, 150), t.fg);
    std::string right = x.valid ? "up " + fmtAge(x.up) : "";
    if (x.battPct >= 0) right = std::to_string(x.battPct) + "%" + (x.battChg ? "+" : "") + "  " + right;
    gfx::text(c, F::small, SCREEN_W - 4, 17, right, t.dim, gfx::Datum::top_right);

    float cpu = x.valid ? x.cpu / 100 : -1;
    float mem = x.valid && x.mem[1] ? (float)x.mem[0] / x.mem[1] : -1;
    float disk = x.valid && x.disk[1] > 0 ? x.disk[0] / x.disk[1] : -1;
    char load[12];
    snprintf(load, sizeof load, "%.1f", x.load[0]);
    gauge(c, 3, 29, 76, "CPU", cpu, x.valid ? pct(x.cpu) : "--", x.valid ? load : "", "load");
    gauge(c, 82, 29, 76, "MEM", mem, mem >= 0 ? pct(mem * 100) : "--", x.valid ? fmtMiB(x.mem[1]) : "", "total");
    char dsub[16];
    snprintf(dsub, sizeof dsub, "%.0fG", x.disk[1] - x.disk[0]);
    gauge(c, 161, 29, 76, "DISK", disk, disk >= 0 ? pct(disk * 100) : "--", disk >= 0 ? dsub : "", "free");

    int sy = 76;
    c.fillRoundRect(3, sy, 234, 20, 4, t.panel);
    gfx::spark(c, 6, sy + 2, 164, 16, st.memHist, st.histLen, st.histPos, HIST, 100, t.accent2);
    gfx::spark(c, 6, sy + 2, 164, 16, st.cpuHist, st.histLen, st.histPos, HIST, 100, t.accent);
    if (x.valid) {
      gfx::text(c, F::small, 176, sy + 2, "rx", t.dim);
      gfx::text(c, F::small, 233, sy + 2, fmtRate(x.net[0]) + "/s", t.fg, gfx::Datum::top_right);
      gfx::text(c, F::small, 176, sy + 11, "tx", t.dim);
      gfx::text(c, F::small, 233, sy + 11, fmtRate(x.net[1]) + "/s", t.fg, gfx::Datum::top_right);
    }

    int ay = 100, ax = 4;
    int working = st.countStatus("working"), waiting = st.countStatus("waiting"), idle = st.countStatus("idle");
    uint32_t now = nowMs();
    auto stat = [&](const char* status, int n, const char* label) {
      gfx::statusDot(c, ax + 4, ay + 5, status, now);
      std::string s = std::to_string(n) + " " + label;
      gfx::text(c, F::text, ax + 11, ay, s, n ? t.fg : t.dim);
      ax += 15 + gfx::width(c, F::text, s);
    };
    stat("working", working, "working");
    stat("waiting", waiting, "waiting");
    stat("idle", idle, "idle");
    if (st.unread) {
      std::string u = std::to_string(st.unread);
      int w = gfx::chip(c, SCREEN_W - 4 - gfx::width(c, F::small, u) - 6, ay + 1, u, t.bg, t.accent);
      (void)w;
    }
    const Agent* top = nullptr;
    for (auto& a : st.agents)
      if (a.status == "waiting" || (!top && a.status == "working")) top = &a;
    if (top) {
      std::string line = top->name + ": " + top->last;
      gfx::text(c, F::small, 4, 113, gfx::ellipsize(c, F::small, line, 232), statusColor(top->status));
    } else if (st.agents.empty()) {
      gfx::text(c, F::small, 4, 113, "No agents yet. Press n, or hold G0 and talk.", t.dim);
    }
  }
};

}  // namespace

Screen* makeHome() { return new Home(); }

void openLauncher() {
  static const char* const labels[] = {"Agents", "New agent", "Shell", "Processes", "Notifications",
                                       "Hosts", "Wi-Fi", "Settings", "About"};
  std::vector<std::string> opts(labels, labels + 9);
  ui().choose("Go to", opts, 0, [](int i) {
    Screen* s = nullptr;
    switch (i) {
      case 0: s = makeAgents(); break;
      case 1: s = makeNewAgent(""); break;
      case 2: s = makeTerminal("", "shell"); break;
      case 3: s = makeProcs(); break;
      case 4: s = makeNotices(); break;
      case 5: s = makeHosts(); break;
      case 6: s = makeWifi(); break;
      case 7: s = makeSettings(); break;
      default: s = makeAbout(); break;
    }
    ui().push(s);
  });
}
