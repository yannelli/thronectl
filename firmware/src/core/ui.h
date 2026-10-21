#pragma once
#include "platform.h"
#include "input.h"
#include <memory>

class Screen {
 public:
  virtual ~Screen() {}
  virtual void onEnter() {}
  virtual void onResume() {}
  virtual bool onKey(const KeyEvent& e) = 0;
  virtual void update(uint32_t now) {}
  virtual void draw(Canvas& c) = 0;
  virtual bool statusBar() const { return true; }
  virtual bool isModal() const { return false; }
  /** True for screens that show live host stats; others get them at the slower dimmed rate. */
  virtual bool wantsStats() const { return false; }
  /** Voice transcript for this screen. Return false to use the default (new agent). */
  virtual bool onVoiceText(const std::string& text) { return false; }
  /** Short label shown while recording, e.g. "to fix login". */
  virtual std::string voiceTarget() const { return "new agent"; }

  /** Wraps a callback so it does nothing once this screen is deleted. */
  template <typename Fn>
  auto guard(Fn fn) {
    std::weak_ptr<bool> w = alive_;
    return [w, fn](auto&&... args) mutable {
      if (!w.expired()) fn(std::forward<decltype(args)>(args)...);
    };
  }

 private:
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

struct Toast {
  std::string kind, title, body;
  uint32_t start = 0, until = 0;
};

class UI {
 public:
  void push(Screen* s);
  void pop();
  void replace(Screen* s);
  void home();
  Screen* top() const { return stack_.empty() ? nullptr : stack_.back(); }
  size_t depth() const { return stack_.size(); }
  /** The top screen that is not a modal dialog: the one visible under any dialogs. */
  Screen* base() const {
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
      if (!(*it)->isModal()) return *it;
    return nullptr;
  }

  void handleKey(const KeyEvent& e);
  void tick(uint32_t now);
  void render(Canvas& c);

  void toast(const std::string& kind, const std::string& title, const std::string& body = "", uint32_t ms = 0);
  void confirm(const std::string& title, const std::string& msg, std::function<void(bool)> cb);
  void choose(const std::string& title, const std::vector<std::string>& options, int selected, std::function<void(int)> cb);
  void input(const std::string& title, const std::string& initial, bool multiline, std::function<void(const std::string&)> cb,
             const std::string& hint = "");

  /** Overlay drawn above everything (voice capture). Not owned. */
  std::function<void(Canvas&)> overlay;

 private:
  std::vector<Screen*> stack_;
  std::vector<Screen*> graveyard_;
  Toast toast_;
};

UI& ui();
void drawStatusBar(Canvas& c);
