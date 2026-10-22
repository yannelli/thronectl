#include "screens.h"
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/store.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

enum Step { HARNESS, PROJECT, MODE, PROMPT, LAUNCH };

class NewAgent : public Screen {
 public:
  explicit NewAgent(const std::string& prompt) {
    prompt_.multiline = true;
    prompt_.set(prompt);
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 16;
  }

  void onEnter() override {
    if (!svc().link->ready()) return;
    loading_ = true;
    app().refreshHarness(false, guard([this] {
      app().refreshProjects(false, guard([this] {
        loading_ = false;
        if (!quickStart()) enter(HARNESS);
      }));
    }));
  }

  bool onKey(const KeyEvent& e) override {
    if (step_ == LAUNCH) {
      if (isEscape(e)) ui().pop();  // the launch goes on; its toast still shows
      return true;
    }
    if (step_ == PROMPT) return promptKey(e);
    if (list_.onKey(e)) return true;
    if (isBack(e)) { back(); return true; }
    if (e.key == K_ENTER || navKey(e) == K_RIGHT) { pick(); return true; }
    if (step_ == PROJECT && e.key == 'c') { customPath(); return true; }
    return false;
  }

  bool typing() const override { return step_ == PROMPT; }

  bool onVoiceText(const std::string& text) override {
    if (step_ == LAUNCH) return true;
    std::string cur = prompt_.text;
    prompt_.set(cur.empty() ? text : cur + " " + text);
    if (step_ == PROMPT && settings().autoSend) launch();
    return true;
  }

  std::string voiceTarget() const override { return "prompt"; }

  void draw(Canvas& c) override {
    const Theme& t = T();
    static const char* const titles[] = {"Pick a harness", "Pick a project", "Pick a mode", "Prompt", "Launching"};
    char right[16];
    snprintf(right, sizeof right, "%d/4", step_ < LAUNCH ? step_ + 1 : 4);
    gfx::header(c, titles[step_], right);
    if (!svc().link->ready()) {
      gfx::emptyState(c, "Not connected", "Pair with a host first (Home, then h).");
      gfx::hints(c, {{"`", "back"}});
      return;
    }
    if (loading_) {
      gfx::spinner(c, SCREEN_W / 2, 70, 10, nowMs(), t.accent);
      gfx::hints(c, {{"`", "back"}});
      return;
    }
    switch (step_) {
      case HARNESS: drawHarness(c); break;
      case PROJECT: drawProjects(c); break;
      case MODE: drawModes(c); break;
      case PROMPT: drawPrompt(c); break;
      case LAUNCH: drawLaunch(c); break;
    }
  }

 private:
  const Harness* harness() const {
    return hIdx_ >= 0 && hIdx_ < (int)store().harness.size() ? &store().harness[hIdx_] : nullptr;
  }

  void enter(Step s) {
    step_ = s;
    list_.first = 0;
    list_.sel = 0;
    Settings& st = settings();
    if (s == HARNESS) {
      auto& hs = store().harness;
      list_.count = (int)hs.size();
      for (int i = 0; i < (int)hs.size(); i++)
        if (hs[i].id == st.lastHarness) list_.sel = i;
    } else if (s == PROJECT) {
      auto& ps = store().projects;
      list_.count = (int)ps.size() + 1;
      for (int i = 0; i < (int)ps.size(); i++)
        if (ps[i].path == st.lastProject) list_.sel = i;
      if (!cwd_.empty()) {
        for (int i = 0; i < (int)ps.size(); i++)
          if (ps[i].path == cwd_) list_.sel = i;
      }
    } else if (s == MODE) {
      const Harness* h = harness();
      if (!h || h->modes.empty()) {
        mode_.clear();
        return enter(PROMPT);
      }
      list_.count = (int)h->modes.size();
      std::string want = h->id == st.lastHarness && !st.lastMode.empty() ? st.lastMode : h->mode;
      for (int i = 0; i < (int)h->modes.size(); i++)
        if (h->modes[i].id == want) list_.sel = i;
    }
    list_.clamp();
  }

  /** A spoken prompt reuses the last harness, folder and mode, so it lands on the prompt step. */
  bool quickStart() {
    Settings& st = settings();
    if (prompt_.text.empty() || st.lastProject.empty()) return false;
    auto& hs = store().harness;
    for (int i = 0; i < (int)hs.size(); i++) {
      if (hs[i].id != st.lastHarness || !hs[i].ok) continue;
      hIdx_ = i;
      cwd_ = st.lastProject;
      mode_ = hs[i].mode;
      for (auto& m : hs[i].modes)
        if (m.id == st.lastMode) mode_ = m.id;
      step_ = PROMPT;
      return true;
    }
    return false;
  }

  void back() {
    switch (step_) {
      case HARNESS: ui().pop(); break;
      case PROJECT: enter(HARNESS); list_.sel = hIdx_; list_.clamp(); break;
      case MODE: enter(PROJECT); break;
      case PROMPT: {
        const Harness* h = harness();
        enter(h && !h->modes.empty() ? MODE : PROJECT);
        break;
      }
      default: break;
    }
  }

  void pick() {
    if (step_ == HARNESS) {
      if (list_.count == 0) return;
      const Harness& h = store().harness[list_.sel];
      if (!h.ok) {
        ui().toast("attn", h.name + " is not installed", "Install it on the host, then reconnect.");
        return;
      }
      hIdx_ = list_.sel;
      enter(PROJECT);
    } else if (step_ == PROJECT) {
      auto& ps = store().projects;
      if (list_.sel >= (int)ps.size()) return customPath();
      cwd_ = ps[list_.sel].path;
      enter(MODE);
    } else if (step_ == MODE) {
      mode_ = harness()->modes[list_.sel].id;
      enter(PROMPT);
    }
  }

  void customPath() {
    ui().input("Project folder", cwd_.empty() ? "~/" : cwd_, false, guard([this](const std::string& v) {
      if (v.empty()) return;
      cwd_ = v;
      enter(MODE);
    }), "Path on the host. ~ is your home folder.");
  }

  bool promptKey(const KeyEvent& e) {
    if (isEscape(e)) { back(); return true; }
    if (e.key == K_ENTER && !e.shift) { launch(); return true; }
    if (e.key == K_TAB) {
      ui().input("Agent name", name_, false, guard([this](const std::string& v) { name_ = v; }), "Empty = derive from the prompt.");
      return true;
    }
    prompt_.onKey(e);
    return true;
  }

  void launch() {
    const Harness* h = harness();
    if (!h) return;
    step_ = LAUNCH;
    Settings& st = settings();
    st.lastHarness = h->id;
    st.lastProject = cwd_;
    st.lastMode = mode_;
    settingsSave();
    JsonDocument p;
    p["harness"] = h->id.c_str();
    p["cwd"] = cwd_.c_str();
    p["prompt"] = prompt_.text.c_str();
    p["name"] = name_.c_str();
    p["mode"] = mode_.c_str();
    std::string title = name_.empty() ? h->name : name_;
    auto retry = guard([this] { step_ = PROMPT; });
    auto open = guard([](const std::string& id, const std::string& name) {
      if (settings().attachAfterCreate) ui().replace(makeTerminal(id, name));
      else ui().replace(makeAgent(id));
    });
    svc().link->call("agent.create", p, [retry, open, title](bool ok, JsonVariantConst r, const char* err) mutable {
      if (!ok) {
        ui().toast("error", "Launch failed", err ? err : "");
        return retry();
      }
      std::string id = r["id"] | "";
      ui().toast("done", "Agent started", title);
      svc().audio->chime(CH_OK);
      open(id, title);
    }, 45000);
  }

  void drawHarness(Canvas& c) {
    const Theme& t = T();
    auto& hs = store().harness;
    if (hs.empty()) {
      gfx::emptyState(c, "No harnesses", "The host reported no agent CLIs.");
      gfx::hints(c, {{"`", "back"}});
      return;
    }
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      const Harness& h = hs[i];
      cv.fillRoundRect(x + 6, y + 4, 8, 8, 2, h.ok ? h.color : t.panel2);
      gfx::text(cv, F::text, x + 19, y + 3, h.name, h.ok ? (sel ? t.selFg : t.fg) : t.dim);
      std::string tag = h.ok ? (h.backend == "paseo" ? "paseo" : "pty") : "missing";
      int tw = gfx::width(cv, F::small, tag) + 6;
      gfx::chip(cv, x + w - tw - 3, y + 3, tag, h.ok ? t.fg : t.dim, h.backend == "paseo" ? blend(t.panel2, t.accent2, 90) : t.panel2);
    });
    gfx::hints(c, {{"ent", "next"}, {"`", "back"}, {"G0", "say prompt"}});
  }

  void drawProjects(Canvas& c) {
    const Theme& t = T();
    auto& ps = store().projects;
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      if (i >= (int)ps.size()) {
        gfx::text(cv, F::text, x + 8, y + 3, "+ Other folder...", t.accent);
        return;
      }
      const Project& p = ps[i];
      int bw = 0;
      if (!p.branch.empty()) {
        std::string b = gfx::ellipsize(cv, F::small, p.branch, 70);
        bw = gfx::width(cv, F::small, b) + 6;
        gfx::chip(cv, x + w - bw - 3, y + 3, b, t.accent2, t.panel2);
      }
      gfx::text(cv, F::text, x + 8, y + 3, gfx::ellipsize(cv, F::text, p.name, w - bw - 16), sel ? t.selFg : t.fg);
    });
    gfx::hints(c, {{"ent", "next"}, {"c", "custom path"}, {"`", "back"}});
  }

  void drawModes(Canvas& c) {
    const Theme& t = T();
    const Harness* h = harness();
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int, int, bool sel) {
      const Mode& m = h->modes[i];
      bool danger = m.id.find("bypass") != std::string::npos || m.id.find("yolo") != std::string::npos ||
                    m.id.find("full") != std::string::npos || m.id == "force";
      gfx::text(cv, F::text, x + 8, y + 3, m.label, danger ? t.err : (sel ? t.selFg : t.fg));
      if (m.id == h->mode) gfx::text(cv, F::small, SCREEN_W - 8, y + 4, "default", t.dim, gfx::Datum::top_right);
    });
    gfx::hints(c, {{"ent", "next"}, {"`", "back"}});
  }

  void drawPrompt(Canvas& c) {
    const Theme& t = T();
    const Harness* h = harness();
    int y = STATUS_H + HEADER_H + 2;
    int x = 4 + gfx::chip(c, 4, y, h ? h->name : "?", t.bg, h ? h->color : t.dim) + 4;
    std::string info = baseName(cwd_);
    if (!mode_.empty()) info += "  " + mode_;
    if (!name_.empty()) info += "  \"" + name_ + "\"";
    gfx::text(c, F::small, x, y + 1, gfx::ellipsize(c, F::small, info, SCREEN_W - x - 4), t.dim);
    prompt_.draw(c, 3, y + 13, SCREEN_W - 6, SCREEN_H - HINT_H - y - 15, true);
    if (prompt_.text.empty()) gfx::text(c, F::small, 9, y + 17, "What should it do? Or hold G0 and say it.", t.dim);
    gfx::hints(c, {{"ent", "launch"}, {"sh+ent", "newline"}, {"tab", "name"}, {"`", "back"}});
  }

  void drawLaunch(Canvas& c) {
    const Theme& t = T();
    gfx::spinner(c, SCREEN_W / 2, 62, 12, nowMs(), t.accent);
    const Harness* h = harness();
    gfx::text(c, F::text, SCREEN_W / 2, 82, "Starting " + (h ? h->name : std::string("agent")), t.fg, gfx::Datum::top_center);
    gfx::text(c, F::small, SCREEN_W / 2, 96, gfx::ellipsize(c, F::small, cwd_, 220), t.dim, gfx::Datum::top_center);
  }

  Step step_ = HARNESS;
  ListView list_;
  TextInput prompt_;
  int hIdx_ = -1;
  std::string cwd_, mode_, name_;
  bool loading_ = false;
};

}  // namespace

Screen* makeNewAgent(const std::string& prompt) { return new NewAgent(prompt); }
