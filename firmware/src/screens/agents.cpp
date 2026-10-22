#include "screens.h"
#include <algorithm>
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/store.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

void call(const char* method, const std::string& id, const char* okMsg, std::function<void(JsonDocument&)> extra = nullptr) {
  JsonDocument p;
  p["id"] = id.c_str();
  if (extra) extra(p);
  std::string ok = okMsg ? okMsg : "";
  svc().link->call(method, p, [ok](bool success, JsonVariantConst, const char* err) {
    if (!success) ui().toast("error", "Failed", err ? err : "");
    else if (!ok.empty()) ui().toast("info", ok);
  });
}

void killAgent(const std::string& id, const std::string& name, std::function<void()> after = nullptr) {
  auto go = [id, after] {
    call("agent.kill", id, "Agent ended");
    if (after) after();
  };
  if (!settings().confirmKill) return go();
  ui().confirm("End agent?", name, [go](bool yes) { if (yes) go(); });
}

void permit(const std::string& id, bool allow) {
  call("agent.permit", id, allow ? "Allowed" : "Denied", [allow](JsonDocument& p) { p["allow"] = allow; });
}

void sendPrompt(const std::string& id, const std::string& text) {
  if (text.empty()) return;
  call("agent.send", id, "Sent", [text](JsonDocument& p) { p["text"] = text.c_str(); });
}

static const char* const FILTERS[] = {"All", "Active", "Waiting"};

class AgentList : public Screen {
 public:
  AgentList() {
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 24;
  }

  void onEnter() override { refresh(); }
  void onResume() override { refresh(); }

  void update(uint32_t) override {
    if (rev_ != store().agentsRev) refresh();
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    const Agent* a = selected();
    if (e.plain() && e.key >= '1' && e.key <= '9') {
      size_t i = e.key - '1';
      if (i < ids_.size()) ui().push(makeAgent(ids_[i]));
      return true;
    }
    switch (e.key) {
      case K_ENTER: if (a) ui().push(makeAgent(a->id)); return true;
      case 'n': ui().push(makeNewAgent("")); return true;
      case 'f': filter_ = (filter_ + 1) % 3; refresh(); return true;
      case 'r': svc().link->call("agents.list", [](bool ok, JsonVariantConst r, const char*) { if (ok) store().applyAgents(r); }); return true;
      default: break;
    }
    if (!a) return false;
    switch (e.key) {
      case 't': ui().push(makeTerminal(a->id, a->name)); return true;
      case 'k': killAgent(a->id, a->name); return true;
      case 'x': call("agent.stop", a->id, "Interrupted"); return true;
      case 'y': if (a->status == "waiting") permit(a->id, true); return true;
      case 'd': if (a->status == "waiting") permit(a->id, false); return true;
      case 'p': {
        std::string id = a->id;
        ui().input("Send to " + a->name, "", true, [id](const std::string& v) { sendPrompt(id, v); });
        return true;
      }
      case 'v': app().speakAgent(a->id); return true;
      default: return false;
    }
  }

  bool onVoiceText(const std::string& text) override {
    const Agent* a = selected();
    if (!a || a->status == "exited") return false;
    sendPrompt(a->id, text);
    return true;
  }

  std::string voiceTarget() const override {
    const Agent* a = selected();
    return a && a->status != "exited" ? a->name : "new agent";
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    char right[32];
    snprintf(right, sizeof right, "%s  %d", FILTERS[filter_], (int)ids_.size());
    gfx::header(c, "Agents", right);
    if (!svc().link->ready()) {
      gfx::emptyState(c, "Not connected", "Agents appear here once the device is linked to a host.");
    } else if (ids_.empty()) {
      gfx::emptyState(c, filter_ ? "Nothing here" : "No agents", filter_ ? "Press f to change the filter." : "Press n to start one, or press space and say a prompt.");
    } else {
      uint32_t now = nowMs();
      list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
        const Agent* a = store().agent(ids_[i]);
        if (!a) return;
        gfx::statusDot(cv, x + 9, y + 7, a->status, now);
        std::string hs = harnessShort(a->harness);
        int cw = gfx::width(cv, F::small, hs) + 6;
        std::string age = fmtAge(a->age);
        int aw = gfx::width(cv, F::small, age);
        gfx::chip(cv, x + w - cw - aw - 8, y + 2, hs, t.bg, harnessColor(a->harness));
        gfx::text(cv, F::small, x + w - 3, y + 3, age, t.dim, gfx::Datum::top_right);
        std::string name = a->name.empty() ? baseName(a->cwd) : a->name;
        gfx::text(cv, F::text, x + 17, y + 1, gfx::ellipsize(cv, F::text, name, w - cw - aw - 30), sel ? t.selFg : t.fg);
        std::string sub = a->status == "waiting" ? "Needs approval: y allow, d deny" : a->last.empty() ? baseName(a->cwd) : a->last;
        gfx::text(cv, F::small, x + 17, y + gfx::height(cv, F::text) + 3, gfx::ellipsize(cv, F::small, sub, w - 22), a->status == "waiting" ? t.warn : t.dim);
      });
    }
    const Agent* a = selected();
    if (a && a->status == "waiting") gfx::hints(c, {{"y", "allow"}, {"d", "deny"}, {"ent", "open"}, {"t", "term"}, {"k", "end"}});
    else gfx::hints(c, {{"ent", "open"}, {"n", "new"}, {"p", "prompt"}, {"t", "term"}, {"f", "filter"}, {"k", "end"}});
  }

 private:
  const Agent* selected() const {
    if (list_.sel < 0 || list_.sel >= (int)ids_.size()) return nullptr;
    return store().agent(ids_[list_.sel]);
  }

  void refresh() {
    std::string cur = list_.sel < (int)ids_.size() ? ids_[list_.sel] : "";
    ids_.clear();
    bool exited = settings().showExited;
    for (auto& a : store().agents) {
      bool active = a.status == "working" || a.status == "waiting";
      if (filter_ == 1 && !active) continue;
      if (filter_ == 2 && a.status != "waiting") continue;
      if (!exited && a.status == "exited") continue;
      if (a.id == cur) list_.sel = (int)ids_.size();
      ids_.push_back(a.id);
    }
    list_.count = (int)ids_.size();
    list_.clamp();
    rev_ = store().agentsRev;
  }

  ListView list_;
  std::vector<std::string> ids_;
  int filter_ = 0;
  uint32_t rev_ = 0;
};

/** Word wrap for the 6 px monospace font. Counts UTF-8 code points. */
void wrapMono(const std::string& s, int cols, std::vector<std::string>& out) {
  std::string line;
  int n = 0, lastSpace = -1, spaceCol = 0;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char ch = s[i];
    if (ch == '\t') ch = ' ';
    if ((ch & 0xC0) != 0x80) {
      if (n >= cols) {
        if (lastSpace > 0 && spaceCol > cols / 2) {
          out.push_back(line.substr(0, lastSpace));
          line = line.substr(lastSpace + 1);
          n -= spaceCol + 1;
        } else {
          out.push_back(line);
          line.clear();
          n = 0;
        }
        lastSpace = -1;
      }
      if (ch == ' ') { lastSpace = (int)line.size(); spaceCol = n; }
      n++;
    }
    line += (char)ch;
  }
  out.push_back(line);
}

class AgentDetail : public Screen {
 public:
  explicit AgentDetail(std::string id) : id_(std::move(id)) {}

  void onEnter() override { poll(); }
  void onResume() override { poll(); }

  void update(uint32_t now) override {
    if (elapsedMs(now, polled_) > 1500 && !inflight_) poll();
  }

  bool onKey(const KeyEvent& e) override {
    if (isBack(e)) { ui().pop(); return true; }
    const Agent* a = store().agent(id_);
    std::string name = a ? a->name : id_;
    uint16_t nav = navKey(e);
    int maxScroll = (int)lines_.size() > rows_ ? (int)lines_.size() - rows_ : 0;
    if (nav == K_UP || nav == K_LEFT) {
      scroll_ += nav == K_UP ? 1 : rows_;
      if (scroll_ > maxScroll) scroll_ = maxScroll;
      return true;
    }
    if (nav == K_DOWN || nav == K_RIGHT) {
      scroll_ -= nav == K_DOWN ? 1 : rows_;
      if (scroll_ < 0) scroll_ = 0;
      return true;
    }
    std::string id = id_;
    if (e.plain() && (e.key == '[' || e.key == ']')) {
      // Swaps this view for the agent before or after this one in the host list.
      auto& all = store().agents;
      for (size_t i = 0; i < all.size(); i++) {
        if (all[i].id != id_ || all.size() < 2) continue;
        std::string next = all[(i + (e.key == ']' ? 1 : all.size() - 1)) % all.size()].id;
        ui().pop();
        ui().push(makeAgent(next));
        return true;
      }
      return true;
    }
    switch (e.key) {
      case K_ENTER: case 'p':
        ui().input("Send to " + name, "", true, [id](const std::string& v) { sendPrompt(id, v); });
        return true;
      case 't': ui().push(makeTerminal(id_, name)); return true;
      case 'y': permit(id_, true); return true;
      case 'd': permit(id_, false); return true;
      case 'x': call("agent.stop", id_, "Interrupted"); return true;
      case 'k': killAgent(id_, name, [] { ui().pop(); }); return true;
      case 'v': app().speakAgent(id_); ui().toast("info", "Reading aloud"); return true;
      case 'l': {
        JsonDocument p;
        p["id"] = id_.c_str();
        svc().link->call("agent.last", p, [name](bool ok, JsonVariantConst r, const char* err) {
          if (!ok) return ui().toast("error", "No reply", err ? err : "");
          std::string text = r["text"] | "";
          ui().push(makeTextView(name, text.empty() ? "(no reply yet)" : text));
        });
        return true;
      }
      default: return false;
    }
  }

  bool onVoiceText(const std::string& text) override {
    sendPrompt(id_, text);
    return true;
  }

  std::string voiceTarget() const override {
    const Agent* a = store().agent(id_);
    return a ? a->name : id_;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    const Agent* a = store().agent(id_);
    uint32_t now = nowMs();
    std::string name = a ? (a->name.empty() ? baseName(a->cwd) : a->name) : id_;
    gfx::header(c, name, a ? a->status : "gone");
    int y = STATUS_H + HEADER_H + 1;
    if (a) {
      gfx::statusDot(c, 8, y + 5, a->status, now);
      int x = 15 + gfx::chip(c, 15, y + 1, harnessShort(a->harness), t.bg, harnessColor(a->harness)) + 4;
      std::string info = baseName(a->cwd) + "  " + fmtAge(a->age) + "  " + a->backend;
      gfx::text(c, F::small, x, y + 2, gfx::ellipsize(c, F::small, info, SCREEN_W - x - 4), t.dim);
    }
    const Font* f = textScale() ? F::small : F::mono;
    int top = y + gfx::height(c, F::small) + 5, lh = gfx::height(c, f) + 1;
    int barH = gfx::height(c, F::text) + 3, bottom = SCREEN_H - HINT_H - (a && a->status == "waiting" ? barH : 0);
    rows_ = (bottom - top - 1) / lh;
    if (wrapped_ != rev_) rewrap(c, f);
    c.fillRect(0, top - 1, SCREEN_W, rows_ * lh + 2, t.panel);
    int first = (int)lines_.size() - rows_ - scroll_;
    if (first < 0) first = 0;
    for (int r = 0; r < rows_ && first + r < (int)lines_.size(); r++) gfx::text(c, f, 2, top + r * lh, lines_[first + r], t.fg);
    if (lines_.empty()) gfx::text(c, F::small, 4, top + 2, inflight_ ? "loading..." : "(no output yet)", t.dim);
    if (scroll_) gfx::chip(c, SCREEN_W - 40, top + 1, "-" + std::to_string(scroll_), t.bg, t.dim);
    if (a && a->status == "waiting") {
      int by = bottom;
      c.fillRect(0, by, SCREEN_W, barH, (now / 500) % 2 ? t.warn : blend(t.warn, t.bg, 60));
      gfx::text(c, F::text, SCREEN_W / 2, by + 2, "Needs approval  y allow  d deny", t.bg, gfx::Datum::top_center);
    }
    gfx::hints(c, {{"ent", "prompt"}, {"t", "term"}, {"l", "reply"}, {"v", "read"}, {"x", "stop"}, {"k", "end"}});
  }

 private:
  static constexpr int COLS = 39;

  /** Small text wraps in the 6 px mono font; Normal text wraps the larger proportional font by width. */
  void rewrap(Canvas& c, const Font* f) {
    std::vector<std::string> out;
    for (const std::string& l : raw_) {
      if (f == F::mono) wrapMono(l, COLS, out);
      else for (std::string& w : gfx::wrap(c, f, l, SCREEN_W - 4)) out.push_back(std::move(w));
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    lines_ = std::move(out);
    wrapped_ = rev_;
  }

  void poll() {
    if (!svc().link->ready()) return;
    inflight_ = true;
    polled_ = nowMs();
    JsonDocument p;
    p["id"] = id_.c_str();
    p["n"] = 40;
    svc().link->call("agent.tail", p, guard([this](bool ok, JsonVariantConst r, const char*) {
      inflight_ = false;
      polled_ = nowMs();
      if (!ok) return;
      // Keep the newest lines only, each cut to a few rows, so a long log cannot fill the heap.
      JsonArrayConst all = r["lines"].as<JsonArrayConst>();
      size_t skip = all.size() > MAX_LINES ? all.size() - MAX_LINES : 0, i = 0;
      raw_.clear();
      for (JsonVariantConst l : all) {
        if (i++ < skip) continue;
        const char* s = l.as<const char*>();
        raw_.emplace_back(s ? s : "", s ? std::min(strlen(s), MAX_LINE_CHARS) : 0);
      }
      rev_++;
    }));
  }

  static constexpr size_t MAX_LINES = 80, MAX_LINE_CHARS = 320;
  std::string id_;
  std::vector<std::string> raw_, lines_;
  uint32_t rev_ = 0, wrapped_ = 0;
  int rows_ = 9;
  int scroll_ = 0;
  bool inflight_ = false;
  uint32_t polled_ = 0;
};

}  // namespace

Screen* makeAgents() { return new AgentList(); }
Screen* makeAgent(const std::string& id) { return new AgentDetail(id); }
