#include "screens.h"
#include <map>
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/store.h"
#include "../core/theme.h"

namespace {

constexpr uint32_t PENDING_POLL_MS = 3000;
constexpr uint32_t ANSWER_HOLD_MS = 5000;  // skip an answered agent until the host reports its new status

/** One card per waiting agent: what it asks, and one key each to allow, deny or comment. */
class Approvals : public Screen {
 public:
  explicit Approvals(bool root) : root_(root) {}

  void onEnter() override { refresh(true); }
  void onResume() override { refresh(true); }

  void update(uint32_t now) override {
    if (rev_ != store().agentsRev) refresh(false);
    if (!id_.empty() && !inflight_ && elapsedMs(now, polled_) > PENDING_POLL_MS) fetch();
  }

  bool onKey(const KeyEvent& e) override {
    if (isBack(e)) {
      if (!root_) ui().pop();
      return true;
    }
    uint16_t nav = navKey(e);
    if (nav == K_LEFT || nav == K_RIGHT || e.key == '[' || e.key == ']') {
      step(nav == K_RIGHT || e.key == ']' ? 1 : -1);
      return true;
    }
    if (nav == K_UP || nav == K_DOWN) {
      scroll_ += nav == K_DOWN ? 1 : -1;
      if (scroll_ < 0) scroll_ = 0;
      return true;
    }
    if (!e.plain() && !e.shift) return false;
    switch (e.key) {
      case 'm': case K_TAB: openLauncher(); return true;
      case 'a': ui().push(makeAgents()); return true;
      case 'n': ui().push(makeNewAgent("")); return true;
      default: break;
    }
    if (id_.empty()) return false;
    switch (e.key) {
      case 'y': case K_ENTER: answer(id_, true, ""); return true;
      case 'd': case 'x': answer(id_, false, ""); return true;
      case 'c': comment(); return true;
      case 'Y': allowAll(); return true;
      case 'o': ui().push(makeAgent(id_)); return true;
      case 't': {
        const Agent* a = store().agent(id_);
        ui().push(makeTerminal(id_, a ? a->name : id_));
        return true;
      }
      default: return false;
    }
  }

  bool onVoiceText(const std::string& text) override {
    if (id_.empty()) return false;
    answer(id_, false, text);
    return true;
  }

  std::string voiceTarget() const override { return id_.empty() ? "new agent" : "comment"; }

  void draw(Canvas& c) override {
    const Theme& t = T();
    char right[24] = "";
    if (!queue_.empty()) snprintf(right, sizeof right, "%d of %d", pos() + 1, (int)queue_.size());
    gfx::header(c, "Approvals", right);
    if (!svc().link->ready()) {
      gfx::emptyState(c, "Not connected", "Approvals show here once the device is linked to a host.");
      gfx::hints(c, {{"m", "menu"}});
      return;
    }
    const Agent* a = id_.empty() ? nullptr : store().agent(id_);
    if (!a) return drawClear(c);
    uint32_t now = nowMs();
    int y = STATUS_H + HEADER_H + 2;
    gfx::statusDot(c, 8, y + gfx::height(c, F::bold) / 2, a->status, now);
    std::string hs = harnessShort(a->harness);
    int cw = gfx::width(c, F::small, hs) + 6;
    gfx::chip(c, SCREEN_W - cw - 3, y + 1, hs, t.bg, harnessColor(a->harness));
    std::string name = a->name.empty() ? baseName(a->cwd) : a->name;
    gfx::text(c, F::bold, 15, y, gfx::ellipsize(c, F::bold, name, SCREEN_W - cw - 24), t.fg);
    y += gfx::height(c, F::bold) + 1;
    std::string tool = tool_.empty() ? "Needs your approval" : tool_;
    gfx::text(c, F::text, 6, y, gfx::ellipsize(c, F::text, tool, SCREEN_W - 12), t.warn);
    y += gfx::height(c, F::text) + 2;

    int barH = gfx::height(c, F::text) + 4, barY = SCREEN_H - HINT_H - barH - 1;
    int lh = gfx::height(c, F::small) + 1, rows = (barY - y - 3) / lh;
    std::string body = text_.empty() ? a->last : text_;
    auto lines = gfx::wrap(c, F::small, body, SCREEN_W - 12);
    if (scroll_ > (int)lines.size() - rows) scroll_ = lines.size() > (size_t)rows ? (int)lines.size() - rows : 0;
    c.fillRoundRect(3, y - 1, SCREEN_W - 6, rows * lh + 3, 3, t.panel);
    for (int r = 0; r < rows && scroll_ + r < (int)lines.size(); r++)
      gfx::text(c, F::small, 6, y + r * lh, lines[scroll_ + r], t.fg);

    const char* const labels[] = {"y allow", "d deny", "c comment"};
    const uint16_t colors[] = {t.ok, t.err, t.accent};
    int w = (SCREEN_W - 12) / 3;
    for (int i = 0; i < 3; i++) {
      int x = 3 + i * (w + 3);
      const char* label = gfx::width(c, F::text, labels[i]) > w - 6 ? labels[i] + 2 : labels[i];  // drop the key when tight
      c.fillRoundRect(x, barY, w, barH, 4, colors[i]);
      gfx::text(c, F::text, x + w / 2, barY + 2, label, t.bg, gfx::Datum::top_center);
    }
    if (queue_.size() > 1) gfx::hints(c, {{", /", "next"}, {"Y", "allow all"}, {"o", "open"}, {"spc", "talk"}});
    else gfx::hints(c, {{"o", "open"}, {"t", "term"}, {"spc", "talk"}, {"`", root_ ? "stay" : "back"}});
  }

 private:
  void drawClear(Canvas& c) {
    const Theme& t = T();
    int cx = SCREEN_W / 2, cy = STATUS_H + HEADER_H + 26;
    c.fillCircle(cx, cy, 14, t.ok);
    c.drawLine(cx - 6, cy, cx - 2, cy + 5, t.bg);
    c.drawLine(cx - 2, cy + 5, cx + 7, cy - 5, t.bg);
    c.drawLine(cx - 6, cy + 1, cx - 2, cy + 6, t.bg);
    c.drawLine(cx - 2, cy + 6, cx + 7, cy - 4, t.bg);
    gfx::text(c, F::bold, cx, cy + 18, "All clear", t.fg, gfx::Datum::top_center);
    char b[48];
    const Store& st = store();
    snprintf(b, sizeof b, "%d working, %d idle", st.countStatus("working"), st.countStatus("idle"));
    gfx::text(c, F::small, cx, cy + 20 + gfx::height(c, F::bold), b, t.dim, gfx::Datum::top_center);
    if (root_) gfx::hints(c, {{"a", "agents"}, {"n", "new"}, {"spc", "talk"}, {"m", "menu"}});
    else gfx::hints(c, {{"a", "agents"}, {"`", "back"}});
  }

  int pos() const {
    for (int i = 0; i < (int)queue_.size(); i++) if (queue_[i] == id_) return i;
    return 0;
  }

  /** Rebuilds the queue of waiting agents and keeps the current card when it is still waiting. */
  void refresh(bool force) {
    rev_ = store().agentsRev;
    uint32_t now = nowMs();
    queue_.clear();
    for (const Agent& a : store().agents) {
      auto it = answered_.find(a.id);
      bool hold = it != answered_.end() && elapsedMs(now, it->second) < ANSWER_HOLD_MS;
      if (a.status == "waiting" && !hold) queue_.push_back(a.id);
    }
    bool keep = false;
    for (auto& q : queue_) keep |= q == id_;
    if (!keep) show(queue_.empty() ? "" : queue_.front());
    else if (force) fetch();
  }

  void show(const std::string& id) {
    if (id == id_) return;
    id_ = id;
    tool_.clear();
    text_.clear();
    scroll_ = 0;
    if (!id_.empty()) fetch();
  }

  void step(int dir) {
    if (queue_.size() < 2) return;
    int n = (int)queue_.size();
    show(queue_[(pos() + dir + n) % n]);
  }

  void fetch() {
    if (!svc().link->ready()) return;
    inflight_ = true;
    polled_ = nowMs();
    JsonDocument p;
    p["id"] = id_.c_str();
    std::string id = id_;
    svc().link->call("agent.pending", p, guard([this, id](bool ok, JsonVariantConst r, const char*) {
      inflight_ = false;
      polled_ = nowMs();
      if (!ok || id != id_) return;
      tool_ = r["tool"] | "";
      text_ = r["text"] | "";
    }));
  }

  void answer(const std::string& id, bool allow, const std::string& message) {
    JsonDocument p;
    p["id"] = id.c_str();
    p["allow"] = allow;
    if (!message.empty()) p["message"] = message.c_str();
    svc().link->call("agent.permit", p, [allow, message](bool ok, JsonVariantConst, const char* err) {
      if (!ok) ui().toast("error", "Answer failed", err ? err : "");
      else ui().toast("done", allow ? "Allowed" : message.empty() ? "Denied" : "Sent your comment", "", 1200);
    });
    answered_[id] = nowMs();
    refresh(false);
  }

  void comment() {
    std::string id = id_;
    ui().input("Tell the agent instead", "", true, guard([this, id](const std::string& v) {
      if (!v.empty()) answer(id, false, v);
    }), "Denies the request and sends this text.");
  }

  void allowAll() {
    std::vector<std::string> ids = queue_;
    ui().confirm("Allow all?", std::to_string(ids.size()) + " waiting requests.", guard([this, ids](bool yes) {
      if (!yes) return;
      for (auto& id : ids) answer(id, true, "");
    }));
  }

  bool root_;
  std::vector<std::string> queue_;
  std::map<std::string, uint32_t> answered_;
  std::string id_, tool_, text_;
  int scroll_ = 0;
  uint32_t rev_ = 0, polled_ = 0;
  bool inflight_ = false;
};

}  // namespace

Screen* makeApprovals(bool root) { return new Approvals(root); }
