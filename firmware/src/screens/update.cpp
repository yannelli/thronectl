#include "screens.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/theme.h"
#include "../core/updater.h"
#include "../core/widgets.h"

namespace {

const char* const LAUNCHER_STEPS = "To install it: reboot, press any key on the Launcher screen, open SD, pick the file and choose Install.";

std::string baseName(const std::string& path) {
  size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

/** "thronectl-cardputer-adv-v0.2.0.bin" shows as "thronectl v0.2.0". */
std::string label(const std::string& name) {
  static const std::string prefix = "thronectl-cardputer-adv-";
  std::string s = name.compare(0, prefix.size(), prefix) ? name : "thronectl " + name.substr(prefix.size());
  if (s.size() > 4 && s.compare(s.size() - 4, 4, ".bin") == 0) s.resize(s.size() - 4);
  return s;
}

void paragraph(Canvas& c, int y, const std::string& s, uint16_t color, int maxLines) {
  auto lines = gfx::wrap(c, F::text, s, SCREEN_W - 16, maxLines);
  for (size_t i = 0; i < lines.size(); i++) gfx::text(c, F::text, 8, y + (int)i * 11, lines[i], color);
}

class UpdateScreen : public Screen {
 public:
  bool onKey(const KeyEvent& e) override {
    Updater& u = updater();
    if (u.active()) {
      if (isBack(e)) {
        ui().confirm("Stop the update?", "The installed firmware stays as it is.", [](bool yes) {
          if (yes) updater().cancel("cancelled on the device");
        });
      }
      return true;
    }
    if (u.phase() == UpdatePhase::Done && !u.toSd()) return true;
    if (u.phase() == UpdatePhase::Done && e.key == K_ENTER) {
      settingsSave();
      svc().sys->reboot();
      return true;
    }
    if (isBack(e) || e.key == K_ENTER) ui().pop();
    return true;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    Updater& u = updater();
    gfx::header(c, "Firmware update", u.version().empty() ? "" : "v" + u.version());
    std::string from = u.fromLink() ? "From " + svc().link->host().name : "From " + baseName(u.sdPath());
    int y = STATUS_H + 18;
    gfx::text(c, F::text, 8, y, gfx::ellipsize(c, F::text, from + (u.toSd() ? " to the SD card" : " to flash"), SCREEN_W - 16), t.dim);
    y += 16;
    switch (u.phase()) {
      case UpdatePhase::Running: {
        float frac = u.size() ? (float)u.written() / (float)u.size() : 0;
        gfx::bar(c, 8, y, SCREEN_W - 16, 10, frac, t.accent, t.panel2);
        char b[48];
        snprintf(b, sizeof b, "%d%%   %u / %u KB", (int)(frac * 100), (unsigned)(u.written() / 1024), (unsigned)(u.size() / 1024));
        gfx::text(c, F::text, 8, y + 16, b, t.fg);
        gfx::text(c, F::text, 8, y + 32, "Keep the device on and near Wi-Fi.", t.dim);
        gfx::hints(c, {{"`", "stop"}});
        break;
      }
      case UpdatePhase::Done:
        if (u.toSd()) {
          gfx::text(c, F::bold, 8, y, "Saved to the SD card", t.ok);
          gfx::text(c, F::small, 8, y + 15, gfx::ellipsize(c, F::small, baseName(u.sdPath()), SCREEN_W - 16), t.dim);
          paragraph(c, y + 27, LAUNCHER_STEPS, t.fg, 4);
          gfx::hints(c, {{"enter", "reboot"}, {"`", "later"}});
        } else {
          gfx::text(c, F::bold, 8, y, "Installed. Restarting...", t.ok);
          gfx::spinner(c, SCREEN_W / 2, y + 40, 8, nowMs(), t.accent);
        }
        break;
      case UpdatePhase::Failed:
        gfx::text(c, F::bold, 8, y, "Update failed", t.err);
        paragraph(c, y + 16, u.error(), t.fg, 4);
        gfx::hints(c, {{"`", "back"}});
        break;
      default:
        gfx::emptyState(c, "No update running", "Run `throne flash` on the host, or use Update from SD card.");
        gfx::hints(c, {{"`", "back"}});
        break;
    }
  }
};

class SdUpdate : public Screen {
 public:
  SdUpdate() {
    list_.y = STATUS_H + 16;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = 12;
  }

  void onEnter() override { scan(); }

  bool onKey(const KeyEvent& e) override {
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    if (e.key == 'r') { scan(); return true; }
    if (e.key == K_ENTER && list_.sel < (int)images_.size()) {
      SdImage img = images_[list_.sel];
      ui().confirm("Install from SD?", img.name + ". It installs, then the device restarts.", guard([img](bool yes) {
        if (!yes) return;
        updater().startSd(img.path);
        ui().replace(makeUpdate());
      }));
      return true;
    }
    return false;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    gfx::header(c, "Update from SD", ready_ ? std::to_string(images_.size()) + " images" : "");
    if (!ready_) {
      gfx::emptyState(c, "No SD card", "Insert a FAT32 card with a thronectl .bin file, then press r.");
    } else if (images_.empty()) {
      gfx::emptyState(c, "No firmware files", "Copy thronectl-cardputer-adv-vX.Y.Z.bin to the card, then press r.");
    } else {
      list_.draw(c, [&](Canvas& cv, int i, int, int y, int, int, bool sel) {
        const SdImage& img = images_[i];
        gfx::text(cv, F::text, 6, y + 1, gfx::ellipsize(cv, F::text, label(img.name), 180), sel ? t.selFg : t.fg);
        gfx::text(cv, F::small, SCREEN_W - 6, y + 2, std::to_string(img.size / 1024) + " KB", sel ? t.selFg : t.dim, gfx::Datum::top_right);
      });
    }
    gfx::hints(c, {{"enter", "install"}, {"r", "rescan"}, {"`", "back"}});
  }

 private:
  void scan() {
    ready_ = svc().ota->sdReady();
    images_ = ready_ ? svc().ota->sdImages() : std::vector<SdImage>();
    list_.count = (int)images_.size();
    list_.clamp();
  }

  ListView list_;
  std::vector<SdImage> images_;
  bool ready_ = false;
};

}  // namespace

Screen* makeUpdate() { return new UpdateScreen(); }

Screen* makeSdUpdate() {
  if (svc().ota->underLauncher())
    return makeTextView("Update from SD", std::string("Launcher installs apps on this device. Copy thronectl-cardputer-adv-vX.Y.Z.bin to the SD card. ") +
                                               LAUNCHER_STEPS + " A host update (`throne flash`) saves the file to the card for you.");
  return new SdUpdate();
}
