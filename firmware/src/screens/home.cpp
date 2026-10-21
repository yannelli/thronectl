#include "screens.h"
#include <cmath>
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/store.h"
#include "../core/theme.h"
#include "dash.h"

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

/** Normal text has a taller value font, so the card grows by 4 px. */
int gaugeH() { return largeText() ? 48 : 44; }

void gauge(Canvas& c, int x, int y, int w, const char* label, float frac, const std::string& value, const std::string& sub,
           const char* caption) {
  const Theme& t = T();
  int grow = gaugeH() - 44;
  c.fillRoundRect(x, y, w, gaugeH(), 4, t.panel);
  gfx::text(c, F::small, x + 4, y + 3, label, t.dim);
  if (!sub.empty() && x + 8 + gfx::width(c, F::big, value) + gfx::width(c, F::small, sub) < x + w) {
    gfx::text(c, F::small, x + w - 4, y + 14, sub, t.fg, gfx::Datum::top_right);
    gfx::text(c, F::small, x + w - 4, y + 24, caption, t.dim, gfx::Datum::top_right);
  }
  uint16_t col = frac < 0 ? t.dim : gfx::loadColor(frac);
  gfx::text(c, F::big, x + 4, y + 13 + grow / 4, value, frac < 0 ? t.dim : t.fg);
  gfx::bar(c, x + 4, y + 36 + grow, w - 8, 4, frac < 0 ? 0 : frac, col, t.panel2);
}

std::string pct(float v) {
  char b[8];
  snprintf(b, sizeof b, "%d%%", (int)lroundf(v));
  return b;
}

class Home : public Screen {
 public:
  bool wantsStats() const override { return true; }
  void update(uint32_t now) override {
    uint32_t every = DASH_CYCLE_MS[settings().dashCycle % 4];
    if (!every || !svc().link->ready()) { flipped_ = now; return; }
    if (elapsedMs(now, flipped_) >= every) {
      slide_ = (slide_ + 1) % dash::COUNT;
      flipped_ = now;
    }
  }

  bool onKey(const KeyEvent& e) override {
    flipped_ = nowMs();
    uint16_t nav = navKey(e);
    if (svc().link->ready() && (nav == K_LEFT || nav == K_RIGHT || e.key == '<' || e.key == '>')) {
      int dir = nav == K_RIGHT || e.key == '>' ? 1 : -1;
      slide_ = (slide_ + dir + dash::COUNT) % dash::COUNT;
      return true;
    }
    if (!e.plain() && !e.shift) return false;
    if (e.key == K_ENTER && slide_ && svc().link->ready()) {
      static Screen* (*const open[])() = {nullptr, makeProcs, makeProcs, makeWifi, nullptr, makeAbout};
      ui().push(open[slide_] ? open[slide_]() : makeAgents());
      return true;
    }
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
    else if (slide_) dash::draw(c, slide_);
    else drawDash(c);
    if (ls != LinkState::Ready) gfx::hints(c, {{"h", "hosts"}, {"w", "wifi"}, {"c", "connect"}, {"s", "settings"}, {"m", "menu"}});
    else if (slide_) gfx::hints(c, {{", /", "slide"}, {"ent", slide_ == 4 ? "agents" : slide_ == 3 ? "wifi" : slide_ == 5 ? "about" : "procs"}, {"n", "new"}, {"m", "menu"}});
    else gfx::hints(c, {{"a", "agents"}, {"n", "new"}, {"t", "term"}, {", /", "more"}, {"m", "menu"}, {"p", "procs"}});
  }

 private:
  int slide_ = 0;
  uint32_t flipped_ = 0;

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
    int sy = cy + 20 + gfx::height(c, F::bold) + 3;
    gfx::text(c, F::text, SCREEN_W / 2, sy, gfx::ellipsize(c, F::text, sub, SCREEN_W - 16), t.dim, gfx::Datum::top_center);
    int retry = app().reconnectIn(now);
    if (retry > 0) {
      std::string r = "Retrying in " + std::to_string(retry) + " s. Press c to retry now.";
      gfx::text(c, F::small, SCREEN_W / 2, sy + LINE_H + 2, r, t.dim, gfx::Datum::top_center);
    }
  }

  void drawDash(Canvas& c) {
    const Theme& t = T();
    Store& st = store();
    const Stats& x = st.stats;
    const HostInfo& h = svc().link->host();
    std::string right = x.valid ? "up " + fmtAge(x.up) : "";
    if (x.battPct >= 0) right = std::to_string(x.battPct) + "%" + (x.battChg ? "+" : "") + "  " + right;
    int gy = dash::titleRow(c, h.name, right, 0);

    float cpu = x.valid ? x.cpu / 100 : -1;
    float mem = x.valid && x.mem[1] ? (float)x.mem[0] / x.mem[1] : -1;
    float disk = x.valid && x.disk[1] > 0 ? x.disk[0] / x.disk[1] : -1;
    char load[12];
    snprintf(load, sizeof load, "%.1f", x.load[0]);
    gauge(c, 3, gy, 76, "CPU", cpu, x.valid ? pct(x.cpu) : "--", x.valid ? load : "", "load");
    gauge(c, 82, gy, 76, "MEM", mem, mem >= 0 ? pct(mem * 100) : "--", x.valid ? fmtMiB(x.mem[1]) : "", "total");
    char dsub[16];
    snprintf(dsub, sizeof dsub, "%.0fG", x.disk[1] - x.disk[0]);
    gauge(c, 161, gy, 76, "DISK", disk, disk >= 0 ? pct(disk * 100) : "--", disk >= 0 ? dsub : "", "free");

    // Normal text has no room for the history strip; the CPU and Network slides show it.
    int sy = gy + gaugeH() + 3, ay = sy + 4;
    if (!largeText()) {
      c.fillRoundRect(3, sy, 234, 20, 4, t.panel);
      gfx::spark(c, 6, sy + 2, 164, 16, st.memHist, st.histLen, st.histPos, HIST, 100, t.accent2);
      gfx::spark(c, 6, sy + 2, 164, 16, st.cpuHist, st.histLen, st.histPos, HIST, 100, t.accent);
      if (x.valid) {
        gfx::text(c, F::small, 176, sy + 2, "rx", t.dim);
        gfx::text(c, F::small, 233, sy + 2, fmtRate(x.net[0]) + "/s", t.fg, gfx::Datum::top_right);
        gfx::text(c, F::small, 176, sy + 11, "tx", t.dim);
        gfx::text(c, F::small, 233, sy + 11, fmtRate(x.net[1]) + "/s", t.fg, gfx::Datum::top_right);
      }
      ay = sy + 24;
    }
    int ax = 4, th = gfx::height(c, F::text);
    int working = st.countStatus("working"), waiting = st.countStatus("waiting"), idle = st.countStatus("idle");
    uint32_t now = nowMs();
    auto stat = [&](const char* status, int n, const char* label) {
      gfx::statusDot(c, ax + 4, ay + th / 2, status, now);
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
      gfx::text(c, F::small, 4, ay + th + 3, gfx::ellipsize(c, F::small, line, 232), statusColor(top->status));
    } else if (st.agents.empty()) {
      gfx::text(c, F::small, 4, ay + th + 3, "No agents yet. Press n, or hold G0 and talk.", t.dim);
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
