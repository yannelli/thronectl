#include "dash.h"
#include <cmath>
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/store.h"
#include "../core/theme.h"

namespace dash {
namespace {

const char* const TITLES[COUNT] = {"Overview", "CPU", "Memory & disk", "Network", "Agents", "Host"};

int bottom() { return SCREEN_H - HINT_H - 2; }

float maxOf(const float* h, int len) {
  float m = 1;
  for (int i = 0; i < len; i++) m = h[i] > m ? h[i] : m;
  return m;
}

std::string gb(float gib) {
  char b[16];
  snprintf(b, sizeof b, gib >= 100 ? "%.0f" : "%.1f", gib);
  return b;
}

/** Label, value on the right, and a bar under both. Returns the next row's y. */
int meter(Canvas& c, int y, const char* label, const std::string& value, float frac) {
  const Theme& t = T();
  gfx::text(c, F::text, 6, y, label, t.fg);
  gfx::text(c, F::small, SCREEN_W - 6, y + 1, value, t.dim, gfx::Datum::top_right);
  int by = y + gfx::height(c, F::text) + 1;
  gfx::bar(c, 6, by, SCREEN_W - 12, 5, frac < 0 ? 0 : frac, frac < 0 ? t.dim : gfx::loadColor(frac), t.panel2);
  return by + 8;
}

void cpu(Canvas& c, int y) {
  const Theme& t = T();
  Store& st = store();
  const Stats& x = st.stats;
  int sparkH = 18, textH = LINE_H, chartH = bottom() - y - sparkH - textH - 6;
  int n = (int)x.cores.size();
  if (n) {
    int gap = n > 16 ? 1 : 2, w = (SCREEN_W - 12 - gap * (n - 1)) / n;
    if (w > 18) w = 18;
    int x0 = (SCREEN_W - (w * n + gap * (n - 1))) / 2;
    for (int i = 0; i < n; i++) {
      float f = x.cores[i] / 100;
      int bh = (int)lroundf(chartH * (f > 1 ? 1 : f < 0 ? 0 : f));
      int bx = x0 + i * (w + gap);
      c.fillRect(bx, y, w, chartH, t.panel);
      c.fillRect(bx, y + chartH - bh, w, bh, gfx::loadColor(f));
    }
  }
  y += chartH + 2;
  char b[48];
  snprintf(b, sizeof b, "load %.1f  %.1f  %.1f", x.load[0], x.load[1], x.load[2]);
  gfx::text(c, F::small, 6, y, b, t.fg);
  gfx::text(c, F::small, SCREEN_W - 6, y, std::to_string(x.procs) + " procs", t.dim, gfx::Datum::top_right);
  y += textH + 1;
  c.fillRoundRect(3, y, SCREEN_W - 6, sparkH, 3, t.panel);
  gfx::spark(c, 6, y + 1, SCREEN_W - 12, sparkH - 2, st.cpuHist, st.histLen, st.histPos, HIST, 100, t.accent);
}

void memory(Canvas& c, int y) {
  const Theme& t = T();
  Store& st = store();
  const Stats& x = st.stats;
  auto used = [](uint32_t u, uint32_t total) { return total ? (float)u / total : -1.0f; };
  y = meter(c, y, "Memory", fmtMiB(x.mem[0]) + " / " + fmtMiB(x.mem[1]), used(x.mem[0], x.mem[1]));
  y = meter(c, y, "Swap", x.swap[1] ? fmtMiB(x.swap[0]) + " / " + fmtMiB(x.swap[1]) : "none", used(x.swap[0], x.swap[1]));
  float df = x.disk[1] > 0 ? x.disk[0] / x.disk[1] : -1;
  y = meter(c, y, "Disk", gb(x.disk[0]) + " / " + gb(x.disk[1]) + " GB", df);
  int sh = bottom() - y;
  if (sh >= 10) {
    c.fillRoundRect(3, y, SCREEN_W - 6, sh, 3, t.panel);
    gfx::spark(c, 6, y + 1, SCREEN_W - 12, sh - 2, st.memHist, st.histLen, st.histPos, HIST, 100, t.accent2);
  }
}

void network(Canvas& c, int y) {
  const Theme& t = T();
  Store& st = store();
  const Stats& x = st.stats;
  Services& s = svc();
  int half = (SCREEN_W - 9) / 2, bh = gfx::height(c, F::small) + gfx::height(c, F::big) + 6;
  const char* labels[2] = {"Down", "Up"};
  uint16_t cols[2] = {t.accent2, t.accent};
  for (int i = 0; i < 2; i++) {
    int bx = 3 + i * (half + 3);
    c.fillRoundRect(bx, y, half, bh, 4, t.panel);
    c.fillRect(bx + 4, y + 4, 3, 3, cols[i]);
    gfx::text(c, F::small, bx + 10, y + 2, labels[i], t.dim);
    gfx::text(c, F::big, bx + 5, y + gfx::height(c, F::small) + 4, fmtRate(x.net[i]) + "/s", t.fg);
  }
  y += bh + 3;
  int info = LINE_H + 1, sh = bottom() - y - info;
  if (sh >= 10) {
    float m = maxOf(st.rxHist, st.histLen);
    float mt = maxOf(st.txHist, st.histLen);
    m = (mt > m ? mt : m) * 1.2f;
    c.fillRoundRect(3, y, SCREEN_W - 6, sh, 3, t.panel);
    gfx::spark(c, 6, y + 1, SCREEN_W - 12, sh - 2, st.rxHist, st.histLen, st.histPos, HIST, m, t.accent2);
    gfx::spark(c, 6, y + 1, SCREEN_W - 12, sh - 2, st.txHist, st.histLen, st.histPos, HIST, m, t.accent);
    y += sh + 2;
  }
  std::string w = s.net->wifiState() == WifiState::Connected ? s.net->ssid() + "  " + std::to_string(s.net->rssi()) + " dBm" : "Wi-Fi off";
  gfx::text(c, F::small, 6, y, gfx::ellipsize(c, F::small, w, 150), t.dim);
  gfx::text(c, F::small, SCREEN_W - 6, y, std::to_string(s.link->rttMs()) + " ms", t.dim, gfx::Datum::top_right);
}

void agents(Canvas& c, int y) {
  const Theme& t = T();
  Store& st = store();
  uint32_t now = nowMs();
  static const char* const kinds[] = {"working", "waiting", "idle", "exited"};
  int x = 6;
  for (const char* k : kinds) {
    int n = st.countStatus(k);
    std::string s = std::to_string(n) + " " + k;
    if (x + 9 + gfx::width(c, F::small, s) > SCREEN_W - 4) break;
    gfx::statusDot(c, x + 3, y + gfx::height(c, F::small) / 2, k, now);
    gfx::text(c, F::small, x + 9, y, s, n ? t.fg : t.dim);
    x += 16 + gfx::width(c, F::small, s);
  }
  y += gfx::height(c, F::small) + 4;
  if (st.agents.empty()) {
    gfx::text(c, F::text, 6, y + 4, "No agents. Press n to start one.", t.dim);
    return;
  }
  int rh = LINE_H + 1;
  for (const Agent& a : st.agents) {
    if (y + rh > bottom()) break;
    gfx::statusDot(c, 9, y + rh / 2, a.status, now);
    std::string hs = harnessShort(a.harness);
    int cw = gfx::width(c, F::small, hs) + 6;
    gfx::chip(c, SCREEN_W - 4 - cw, y + 1, hs, t.bg, harnessColor(a.harness));
    std::string name = a.name.empty() ? baseName(a.cwd) : a.name;
    gfx::text(c, F::text, 17, y, gfx::ellipsize(c, F::text, name, SCREEN_W - 30 - cw), a.status == "exited" ? t.dim : t.fg);
    y += rh;
  }
}

void host(Canvas& c, int y) {
  const Theme& t = T();
  Services& s = svc();
  const HostInfo& h = s.link->host();
  const Stats& x = store().stats;
  std::vector<std::pair<const char*, std::string>> rows = {
      {"Hostname", h.hostname}, {"System", h.os + " " + h.arch}, {"User", h.user}, {"Uptime", fmtAge(x.up)},
      {"Daemon", h.ver + (h.paseo ? "  paseo" : "")}, {"Latency", std::to_string(s.link->rttMs()) + " ms"}};
  if (x.battPct >= 0) rows.insert(rows.begin() + 4, {"Battery", std::to_string(x.battPct) + "%" + (x.battChg ? " charging" : "")});
  int lh = gfx::height(c, F::small) + 2, vx = gfx::width(c, F::small, "Hostname") + 14;
  for (auto& r : rows) {
    if (y + lh > bottom() + 2) break;
    gfx::text(c, F::small, 6, y, r.first, t.dim);
    gfx::text(c, F::small, vx, y, gfx::ellipsize(c, F::small, r.second, SCREEN_W - vx - 6), t.fg);
    y += lh;
  }
}

}  // namespace

const char* title(int slide) { return TITLES[slide % COUNT]; }

int titleRow(Canvas& c, const std::string& name, const std::string& right, int slide) {
  const Theme& t = T();
  int y = STATUS_H + 2, bh = gfx::height(c, F::bold);
  int dotsW = COUNT * 6, dx = SCREEN_W - 5 - dotsW;
  for (int i = 0; i < COUNT; i++) {
    int cx = dx + i * 6 + 3, cy = y + bh / 2;
    if (i == slide) c.fillCircle(cx, cy, 2, t.accent);
    else c.drawCircle(cx, cy, 2, t.dim);
  }
  int rw = right.empty() ? 0 : gfx::width(c, F::small, right) + 6;
  gfx::text(c, F::small, dx - 4, y + bh - gfx::height(c, F::small), right, t.dim, gfx::Datum::top_right);
  gfx::text(c, F::bold, 4, y, gfx::ellipsize(c, F::bold, name, dx - 8 - rw), t.fg);
  return y + bh + 3;
}

void draw(Canvas& c, int slide) {
  const Stats& x = store().stats;
  std::string right;
  switch (slide) {
    case 1: right = std::to_string(x.cores.size()) + " cores"; break;
    case 2: right = x.mem[1] ? fmtMiB(x.mem[1]) + " RAM" : ""; break;
    case 4: right = std::to_string(store().agents.size()) + " total"; break;
    case 5: right = svc().link->host().name; break;
    default: break;
  }
  int y = titleRow(c, title(slide), right, slide);
  if (!x.valid && slide != 4 && slide != 5) {
    gfx::text(c, F::text, SCREEN_W / 2, y + 30, "Waiting for host stats...", T().dim, gfx::Datum::top_center);
    return;
  }
  switch (slide) {
    case 1: cpu(c, y); break;
    case 2: memory(c, y); break;
    case 3: network(c, y); break;
    case 4: agents(c, y); break;
    case 5: host(c, y); break;
    default: break;
  }
}

}  // namespace dash
