#include "screens.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

/** One daemon setting as config.fields sends it. Groups use the same row with type "link". */
struct HostField {
  std::string key, label, type, value, help, confirm;
  std::vector<std::string> options;
  bool on = false, restart = false, live = false;  // live: the value changes on its own, so poll it
};

constexpr uint32_t LIVE_POLL_MS = 2000;

std::string firstLine(const std::string& s) {
  size_t nl = s.find('\n');
  if (nl == std::string::npos) return s;
  int more = 1;
  for (size_t i = nl + 1; i < s.size(); i++) more += s[i] == '\n';
  return s.substr(0, nl) + " +" + std::to_string(more);
}

/** A generic editor for the daemon's settings. An empty group lists the groups. */
class HostConfig : public Screen {
 public:
  HostConfig(std::string group, std::string title) : group_(std::move(group)), title_(std::move(title)) {
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = LINE_H + 3;
  }

  void onEnter() override { load(); }
  void onResume() override { if (dirty_) load(); }

  void update(uint32_t now) override {
    if (live_ && !loading_ && ui().top() == this && elapsedMs(now, loadedAt_) > LIVE_POLL_MS) load();
  }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == 'r') { load(); return true; }
    if (fields_.empty() || list_.sel >= (int)fields_.size()) return false;
    HostField& f = fields_[list_.sel];
    uint16_t nav = navKey(e);
    if ((nav == K_LEFT || nav == K_RIGHT) && (f.type == "bool" || f.type == "choice")) {
      if (f.type == "bool") {
        set(f.key, !f.on);
        return true;
      }
      int n = (int)f.options.size(), i = index(f);
      if (n) set(f.key, f.options[(i + (nav == K_RIGHT ? 1 : -1) + n) % n]);
      return true;
    }
    if (e.key != K_ENTER && nav != K_RIGHT) return false;
    open(f);
    return true;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    gfx::header(c, title_.c_str());
    if (!svc().link->ready()) {
      gfx::emptyState(c, "Not connected", "Connect to a host to change its settings.");
      return gfx::hints(c, {{"`", "back"}});
    }
    if (loading_ && fields_.empty()) return gfx::emptyState(c, "Loading", "");
    if (!err_.empty()) {
      gfx::emptyState(c, "Could not load", err_.c_str());
      return gfx::hints(c, {{"r", "retry"}, {"`", "back"}});
    }
    list_.draw(c, [&](Canvas& cv, int i, int x, int y, int w, int, bool sel) {
      const HostField& f = fields_[i];
      uint16_t fg = sel ? t.selFg : t.fg;
      bool add = f.type == "add";
      gfx::text(cv, F::text, x + 6, y + 2, add ? "+ " + f.label : f.label, add && !sel ? t.accent : fg);
      int rx = x + w - 5, room = w - 19 - gfx::width(cv, F::text, f.label);
      if (f.type == "bool") {
        cv.fillRoundRect(rx - 18, y + 3, 18, 9, 4, f.on ? t.accent : t.panel2);
        cv.fillCircle(f.on ? rx - 5 : rx - 14, y + 7, 3, f.on ? t.bg : t.dim);
        return;
      }
      if (f.type == "link" || f.type == "action") {
        cv.drawLine(rx - 4, y + 3, rx, y + 7, t.dim);
        cv.drawLine(rx, y + 7, rx - 4, y + 11, t.dim);
        rx -= 9;
        room -= 9;
      }
      if (add || (f.type == "action" && f.value.empty())) return;
      std::string v = f.type == "list" ? firstLine(f.value) : f.value;
      uint16_t col = group_.empty() ? t.dim : f.value == "ready" ? t.ok : f.value == "missing" ? t.warn : f.value == "off" ? t.dim : t.accent;
      if (v.empty()) v = f.type == "secret" ? "not set" : f.type == "link" ? "" : "(default)", col = t.dim;
      gfx::text(cv, F::text, rx, y + 2, gfx::ellipsize(cv, F::text, v, room), sel ? t.selFg : col, gfx::Datum::top_right);
    });
    if (fields_.empty()) gfx::emptyState(c, "Nothing here", "");
    const HostField* f = list_.sel < (int)fields_.size() ? &fields_[list_.sel] : nullptr;
    if (f && (f->type == "bool" || f->type == "choice")) gfx::hints(c, {{", /", "change"}, {"ent", "pick"}, {"`", "back"}});
    else if (f && f->type == "secret") gfx::hints(c, {{"ent", "replace"}, {"`", "back"}});
    else gfx::hints(c, {{"ent", "open"}, {"r", "reload"}, {"`", "back"}});
  }

 private:
  void load() {
    dirty_ = false;
    if (!svc().link->ready()) return;
    loading_ = true;
    err_.clear();
    JsonDocument p;
    p.to<JsonObject>();
    if (!group_.empty()) p["group"] = group_.c_str();
    svc().link->call(group_.empty() ? "config.groups" : "config.fields", p, guard([this](bool ok, JsonVariantConst r, const char* err) {
      loading_ = false;
      loadedAt_ = nowMs();
      if (!ok) {
        err_ = err ? err : "No reply";
        fields_.clear();
        return;
      }
      fields_.clear();
      if (group_.empty()) {
        for (JsonVariantConst g : r["groups"].as<JsonArrayConst>())
          fields_.push_back({g["id"] | "", g["label"] | "", "link", g["sub"] | ""});
      } else {
        for (JsonVariantConst j : r["fields"].as<JsonArrayConst>()) fields_.push_back(parse(j));
      }
      live_ = false;
      for (auto& f : fields_) live_ |= f.live;
      list_.count = (int)fields_.size();
      list_.clamp();
    }));
  }

  static HostField parse(JsonVariantConst j) {
    HostField f{j["key"] | "", j["label"] | "", j["type"] | "text", "", j["help"] | ""};
    JsonVariantConst v = j["value"];
    if (v.is<bool>()) f.on = v.as<bool>(), f.value = f.on ? "on" : "off";
    else if (v.is<int>()) f.value = std::to_string(v.as<int>());
    else if (v.is<float>()) { char b[24]; snprintf(b, sizeof b, "%g", v.as<float>()); f.value = b; }
    else f.value = v | "";
    for (JsonVariantConst o : j["options"].as<JsonArrayConst>()) f.options.push_back(o | "");
    f.restart = j["restart"] | false;
    f.live = j["live"] | false;
    f.confirm = j["confirm"] | "";
    return f;
  }

  static int index(const HostField& f) {
    for (int i = 0; i < (int)f.options.size(); i++) if (f.options[i] == f.value) return i;
    return 0;
  }

  void open(HostField& f) {
    std::string key = f.key, label = f.label;
    if (f.type == "link") {
      ui().push(makeHostConfig(key, label));
      dirty_ = true;
    } else if (f.type == "bool") {
      set(key, !f.on);
    } else if (f.type == "choice") {
      ui().choose(label, f.options, index(f), guard([this, key](int i) {
        for (auto& x : fields_) if (x.key == key && i >= 0 && i < (int)x.options.size()) return set(key, x.options[i]);
      }));
    } else if (f.type == "secret") {
      ui().input(label, "", false, guard([this, key, label](const std::string& v) {
        if (!v.empty()) return set(key, v);
        ui().confirm("Remove " + label + "?", "Agents started after this do not get it.", guard([this, key](bool yes) {
          if (yes) setNull(key);
        }));
      }), "Type a new value. Leave it empty to remove it.");
    } else if (f.type == "add") {
      addFlow(key);
    } else if (f.type == "action") {
      runAction(key, label, f.confirm);
    } else {
      ui().input(label, f.value, f.type == "list", guard([this, key](const std::string& v) { set(key, v); }), f.help);
    }
  }

  void addFlow(const std::string& kind) {
    bool env = kind == "env";
    ui().input(env ? "Variable name" : "Harness id", "", false, guard([this, kind, env](const std::string& id) {
      if (id.empty()) return;
      ui().input(env ? id : "Command for " + id, "", false, guard([this, kind, id](const std::string& v) {
        JsonDocument p;
        p["kind"] = kind.c_str();
        p["id"] = id.c_str();
        p["value"] = v.c_str();
        svc().link->call("config.add", p, guard([this](bool ok, JsonVariantConst, const char* err) {
          if (!ok) return ui().toast("error", "Could not add it", err ? err : "");
          ui().toast("done", "Added");
          load();
        }));
      }), env ? "The value. New agents get it." : "A program on the host's PATH, or a full path.");
    }), env ? "For example OPENAI_API_KEY" : "Lower-case letters, digits, - and _");
  }

  void runAction(const std::string& key, const std::string& label, const std::string& confirm) {
    auto run = guard([this, key](bool yes) {
      if (!yes) return;
      JsonDocument p;
      p["key"] = key.c_str();
      bool remove = key.rfind("remove:", 0) == 0;
      svc().link->call("config.action", p, guard([this, remove](bool ok, JsonVariantConst r, const char* err) {
        if (!ok) return ui().toast("error", "Failed", err ? err : "");
        ui().toast("done", r["message"] | "Done");
        if (remove) ui().pop();
        else load();
      }));
    });
    if (confirm.empty()) run(true);
    else ui().confirm(label + "?", confirm, run);
  }

  template <typename V>
  void set(const std::string& key, V value) {
    JsonDocument p;
    p["key"] = key.c_str();
    p["value"] = value;
    send(p);
  }
  void set(const std::string& key, const std::string& value) { set(key, value.c_str()); }

  void setNull(const std::string& key) {
    JsonDocument p;
    p["key"] = key.c_str();
    p["value"] = nullptr;
    send(p);
  }

  void send(JsonDocument& p) {
    svc().link->call("config.set", p, guard([this](bool ok, JsonVariantConst r, const char* err) {
      if (!ok) return ui().toast("error", "Not saved", err ? err : "");
      if (r["restart"] | false) ui().toast("info", "Saved", "Restart the daemon to apply it: Network, Restart daemon.");
      load();
    }));
  }

  std::string group_, title_, err_;
  std::vector<HostField> fields_;
  bool loading_ = false, dirty_ = false, live_ = false;
  uint32_t loadedAt_ = 0;
  ListView list_;
};

}  // namespace

Screen* makeHostConfig(const std::string& group, const std::string& title) { return new HostConfig(group, title); }
