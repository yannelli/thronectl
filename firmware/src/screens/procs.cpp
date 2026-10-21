#include "screens.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/store.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

class Procs : public Screen {
 public:
  Procs() {
    list_.y = STATUS_H + HEADER_H + LINE_H - 1;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 11;
  }

  void onEnter() override { poll(); }

  void update(uint32_t now) override {
    if (!inflight_ && elapsedMs(now, polled_) > 2500) poll();
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == 's') { byMem_ = !byMem_; poll(); return true; }
    if (e.key == 'r') { poll(); return true; }
    if ((e.key == 'k' || e.key == 'K') && list_.sel < (int)procs_.size()) {
      Proc p = procs_[list_.sel];
      bool hard = e.key == 'K';
      char msg[64];
      snprintf(msg, sizeof msg, "%s (pid %d) with SIG%s", p.name.c_str(), p.pid, hard ? "KILL" : "TERM");
      ui().confirm(hard ? "Force kill?" : "Terminate?", msg, guard([this, p, hard](bool yes) {
        if (!yes) return;
        JsonDocument q;
        q["pid"] = p.pid;
        q["sig"] = hard ? "KILL" : "TERM";
        svc().link->call("procs.kill", q, guard([this](bool ok, JsonVariantConst, const char* err) {
          if (ok) ui().toast("done", "Signal sent");
          else ui().toast("error", "Kill failed", err ? err : "");
          poll();
        }));
      }));
      return true;
    }
    return false;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    const Stats& s = store().stats;
    char right[40];
    snprintf(right, sizeof right, "%u procs  by %s", (unsigned)s.procs, byMem_ ? "mem" : "cpu");
    gfx::header(c, "Processes", right);
    int hy = STATUS_H + HEADER_H + 1;
    const int px = gfx::width(c, F::small, "00000") + 5, nx = px + 6;
    gfx::text(c, F::small, px, hy, "PID", t.dim, gfx::Datum::top_right);
    gfx::text(c, F::small, nx, hy, "NAME", t.dim);
    gfx::text(c, F::small, 196, hy, "CPU", byMem_ ? t.dim : t.accent, gfx::Datum::top_right);
    gfx::text(c, F::small, 234, hy, "MEM", byMem_ ? t.accent : t.dim, gfx::Datum::top_right);
    if (!svc().link->ready()) {
      gfx::emptyState(c, "Not connected", "Connect to a host to see its processes.");
    } else if (procs_.empty()) {
      gfx::spinner(c, SCREEN_W / 2, 75, 8, nowMs(), t.accent);
    } else {
      list_.draw(c, [&](Canvas& cv, int i, int, int y, int, int, bool sel) {
        const Proc& p = procs_[i];
        char num[16];
        snprintf(num, sizeof num, "%d", p.pid);
        gfx::text(cv, F::small, px, y + 2, num, t.dim, gfx::Datum::top_right);
        gfx::text(cv, F::small, nx, y + 2, gfx::ellipsize(cv, F::small, p.name, 156 - nx), sel ? t.selFg : t.fg);
        snprintf(num, sizeof num, "%.1f", p.cpu);
        gfx::text(cv, F::small, 196, y + 2, num, p.cpu > 50 ? t.err : p.cpu > 15 ? t.warn : t.fg, gfx::Datum::top_right);
        snprintf(num, sizeof num, "%.1f", p.mem);
        gfx::text(cv, F::small, 232, y + 2, num, p.mem > 10 ? t.warn : t.fg, gfx::Datum::top_right);
      });
    }
    gfx::hints(c, {{"s", "sort"}, {"k", "term"}, {"K", "kill"}, {"r", "refresh"}, {"`", "back"}});
  }

 private:
  void poll() {
    if (!svc().link->ready()) return;
    inflight_ = true;
    polled_ = nowMs();
    JsonDocument p;
    p["sort"] = byMem_ ? "mem" : "cpu";
    p["n"] = 30;
    svc().link->call("procs.list", p, guard([this](bool ok, JsonVariantConst r, const char*) {
      inflight_ = false;
      polled_ = nowMs();
      if (!ok) return;
      int keepPid = list_.sel < (int)procs_.size() ? procs_[list_.sel].pid : 0;
      procs_.clear();
      for (JsonObjectConst o : r["procs"].as<JsonArrayConst>()) {
        Proc p;
        p.pid = o["pid"] | 0;
        p.name = o["name"] | "";
        p.user = o["user"] | "";
        p.cpu = o["cpu"] | 0.0f;
        p.mem = o["mem"] | 0.0f;
        if (p.pid == keepPid) list_.sel = (int)procs_.size();
        procs_.push_back(p);
      }
      list_.count = (int)procs_.size();
      list_.clamp();
    }));
  }

  ListView list_;
  std::vector<Proc> procs_;
  bool byMem_ = false, inflight_ = false;
  uint32_t polled_ = 0;
};

}  // namespace

Screen* makeProcs() { return new Procs(); }
