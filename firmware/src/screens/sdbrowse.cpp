#include "screens.h"
#include "../core/gfx.h"
#include "../core/services.h"
#include "../core/settings.h"
#include "../core/theme.h"
#include "../core/widgets.h"

namespace {

const char* const BACKUP_NAME = "thronectl-settings.json";

bool isJson(const std::string& name) {
  if (name.size() < 6) return false;
  std::string ext = name.substr(name.size() - 5);
  for (char& ch : ext) ch = (char)tolower((unsigned char)ch);
  return ext == ".json";
}

std::string join(const std::string& dir, const std::string& name) { return dir == "/" ? "/" + name : dir + "/" + name; }

std::string parent(const std::string& dir) {
  size_t slash = dir.rfind('/');
  return slash == 0 || slash == std::string::npos ? "/" : dir.substr(0, slash);
}

/** Walks the card's folders. Save mode writes the backup into the open folder; restore mode opens a .json file. */
class SdBrowser : public Screen {
 public:
  explicit SdBrowser(bool save) : save_(save) {
    list_.y = STATUS_H + HEADER_H;
    list_.h = SCREEN_H - HINT_H - list_.y;
    list_.rowH = LINE_H + 1;
    const std::string& last = settings().sdDir;
    dir_ = last.empty() || last[0] != '/' ? "/" : last;
  }

  void onEnter() override { rescan(); }

  void update(uint32_t now) override {
    // Format after one frame with the notice is on screen: it blocks the loop until it is done.
    if (!formatAt_ || elapsedMs(now, formatAt_) < 150) return;
    formatAt_ = 0;
    bool ok = svc().ota->sdFormat();
    if (ok) ui().toast("done", "Card formatted");
    else ui().toast("error", "Format failed", svc().ota->error());
    rescan();
  }

  bool onKey(const KeyEvent& e) override {
    if (formatAt_) return true;
    if (list_.onKey(e)) return true;
    if (isBack(e)) { ui().pop(); return true; }
    uint16_t nav = navKey(e);
    if (nav == K_LEFT) { up(); return true; }
    if (!ready_) {
      if (e.key == 'r') rescan();
      else if (e.key == 'f' && svc().ota->sdNeedsFormat()) format();
      else return false;
      return true;
    }
    if (e.key == 'r') { rescan(); return true; }
    if (e.key == 'n') { newFolder(); return true; }
    if (e.key == 's' && save_) { saveHere(); return true; }
    if (e.key == K_ENTER || nav == K_RIGHT) { open(list_.sel, e.key == K_ENTER); return true; }
    return false;
  }

  void draw(Canvas& c) override {
    const Theme& t = T();
    gfx::header(c, save_ ? "Back up to SD" : "Restore from SD", ready_ ? gfx::ellipsize(c, F::small, dir_, 120) : "");
    if (formatAt_) return gfx::emptyState(c, "Formatting", "This can take a minute on a large card.");
    if (!ready_) {
      bool fmt = svc().ota->sdNeedsFormat();
      gfx::emptyState(c, fmt ? "Card needs FAT32" : "No SD card", err_.c_str());
      if (fmt) gfx::hints(c, {{"f", "format"}, {"r", "retry"}, {"`", "back"}});
      else gfx::hints(c, {{"r", "retry"}, {"`", "back"}});
      return;
    }
    list_.draw(c, [&](Canvas& cv, int i, int, int y, int, int, bool sel) {
      uint16_t fg = sel ? t.selFg : t.fg, dim = sel ? t.selFg : t.dim;
      const Row& r = rows_[i];
      if (r.kind == Row::SAVE) {
        gfx::text(cv, F::bold, 6, y + 1, "Save here", sel ? t.selFg : t.accent);
        int room = SCREEN_W - 30 - gfx::width(cv, F::bold, "Save here");
        if (gfx::width(cv, F::small, BACKUP_NAME) <= room)
          gfx::text(cv, F::small, SCREEN_W - 6, y + 2, BACKUP_NAME, dim, gfx::Datum::top_right);
        return;
      }
      if (r.kind == Row::UP) {
        gfx::text(cv, F::text, 6, y + 1, ".. up one folder", dim);
        return;
      }
      const SdEntry& en = entries_[r.idx];
      int x = 6;
      if (en.dir) {
        int fy = y + 3;
        cv.fillRect(x, fy, 4, 2, dim);
        cv.fillRect(x, fy + 2, 10, 7, dim);
        x += 14;
      }
      std::string right = en.dir ? "" : std::to_string((en.size + 1023) / 1024) + " KB";
      gfx::text(cv, F::text, x, y + 1, gfx::ellipsize(cv, F::text, en.name, SCREEN_W - x - 50), en.dir ? fg : dim);
      if (!right.empty()) gfx::text(cv, F::small, SCREEN_W - 6, y + 2, right, dim, gfx::Datum::top_right);
    });
    if (rows_.empty()) gfx::emptyState(c, "No backups here", "Open a folder, or press left to go up.");
    if (save_) gfx::hints(c, {{"s", "save here"}, {"ent", "open"}, {"n", "new"}, {",", "up"}});
    else gfx::hints(c, {{"ent", "open"}, {",", "up"}, {"r", "rescan"}, {"`", "back"}});
  }

 private:
  struct Row {
    enum Kind { SAVE, UP, ENTRY } kind;
    int idx = -1;
  };

  void rescan() {
    ready_ = svc().ota->sdReady();
    if (ready_ && !list(dir_)) list(dir_ = "/");
    if (!ready_) err_ = svc().ota->sdNeedsFormat() ? "exFAT does not work. Press f to format it, or use a computer."
                                                   : "Insert a FAT32 card, then press r.";
  }

  bool list(const std::string& dir) {
    std::vector<SdEntry> got;
    if (!svc().ota->sdList(dir, got)) return false;
    entries_.clear();
    for (auto& en : got)
      if (en.dir || isJson(en.name)) entries_.push_back(en);
    rows_.clear();
    if (save_) rows_.push_back({Row::SAVE});
    if (dir_ != "/") rows_.push_back({Row::UP});
    for (int i = 0; i < (int)entries_.size(); i++) rows_.push_back({Row::ENTRY, i});
    list_.count = (int)rows_.size();
    list_.sel = 0;
    list_.first = 0;
    // In restore mode, start on the newest backup name when the folder has one.
    for (int i = 0; !save_ && i < (int)rows_.size(); i++)
      if (rows_[i].kind == Row::ENTRY && entries_[rows_[i].idx].name == BACKUP_NAME) list_.sel = i;
    list_.clamp();
    return true;
  }

  void enter(const std::string& dir) {
    std::string prev = dir_;
    dir_ = dir;
    if (!list(dir_)) {
      ui().toast("error", "Could not open the folder", svc().ota->error());
      dir_ = prev;
    }
  }

  void up() {
    if (ready_ && dir_ != "/") enter(parent(dir_));
  }

  void open(int sel, bool enterKey) {
    if (sel < 0 || sel >= (int)rows_.size()) return;
    const Row& r = rows_[sel];
    if (r.kind == Row::SAVE) { if (enterKey) saveHere(); return; }
    if (r.kind == Row::UP) return up();
    const SdEntry& en = entries_[r.idx];
    if (en.dir) return enter(join(dir_, en.name));
    if (!enterKey) return;
    std::string path = join(dir_, en.name);
    if (save_) {
      ui().confirm("Replace this file?", path + " gets the current settings.", guard([this, path](bool yes) {
        if (yes) write(path);
      }));
      return;
    }
    std::string dir = dir_;
    ui().confirm("Restore settings?", "Replaces all settings with " + path + ".", [path, dir](bool yes) {
      if (!yes) return;
      settings().sdDir = dir;
      restoreSettingsFile(path);
    });
  }

  void saveHere() {
    std::string path = join(dir_, BACKUP_NAME);
    bool exists = false;
    for (auto& en : entries_) exists |= !en.dir && en.name == BACKUP_NAME;
    if (!exists) return write(path);
    ui().confirm("Replace the backup?", path + " is already there.", guard([this, path](bool yes) {
      if (yes) write(path);
    }));
  }

  void write(const std::string& path) {
    Settings& s = settings();
    s.sdDir = dir_;
    if (!svc().ota->sdWriteFile(path, settingsToJson(s))) {
      ui().toast("error", "Could not write the SD card", svc().ota->error());
      return;
    }
    settingsSave();
    ui().toast("done", "Settings saved to SD", path + ". It holds Wi-Fi passwords.");
    ui().pop();
  }

  void newFolder() {
    ui().input("New folder", "", false, guard([this](const std::string& v) {
      std::string name = v;
      for (char& ch : name) if (ch == '/' || ch == '\\' || ch == ':') ch = '-';
      if (name.empty() || name[0] == '.') return;
      std::string path = join(dir_, name);
      if (!svc().ota->sdMkdir(path)) return ui().toast("error", "Could not make the folder", svc().ota->error());
      enter(path);
    }), "Made inside " + dir_ + ".");
  }

  void format() {
    ui().confirm("Format the card?", "Erases everything on it and makes one FAT32 volume.", guard([this](bool yes) {
      if (yes) formatAt_ = nowMs() | 1;
    }));
  }

  bool save_;
  bool ready_ = false;
  uint32_t formatAt_ = 0;
  std::string dir_, err_;
  std::vector<SdEntry> entries_;
  std::vector<Row> rows_;
  ListView list_;
};

}  // namespace

Screen* makeSdBrowser(bool save) { return new SdBrowser(save); }
