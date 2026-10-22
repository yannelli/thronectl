// Host check for the flash settings store: atomic saves, wipe and the Bluetooth boot gate.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include "core/filestore.h"
#include "core/theme.h"

const Theme& T() {
  static const Theme theme{"test", 0, 0, 0, 0, 0, 0xf800, 0, 0, 0, 0, 0, 0};
  return theme;
}

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

static int entries(const std::string& dir) {
  DIR* d = opendir(dir.c_str());
  if (!d) return -1;
  int n = 0;
  while (dirent* e = readdir(d)) n += e->d_name[0] != '.';
  closedir(d);
  return n;
}

static void writeFile(const std::string& path, const char* text) {
  FILE* f = fopen(path.c_str(), "wb");
  fputs(text, f);
  fclose(f);
}

int main() {
  char tmpl[] = "/tmp/storecheck-XXXXXX";
  std::string root = mkdtemp(tmpl), dir = root + "/thronectl";
  FileStore fs(dir);

  check(fs.load("cfg").empty() && entries(dir) == -1, "a fresh volume has no settings and no folder");
  fs.save("cfg", "{\"a\":1}");
  check(fs.load("cfg") == "{\"a\":1}" && entries(dir) == 1, "save creates the folder and one file, no temp file left");
  fs.save("cfg", "{\"b\":22}");
  check(fs.load("cfg") == "{\"b\":22}" && entries(dir) == 1, "a second save replaces the file");

  writeFile(dir + "/cfg.tmp", "{\"half");
  check(fs.load("cfg") == "{\"b\":22}", "a temp file left by a power cut does not change what loads");
  fs.save("cfg", "{\"c\":3}");
  check(fs.load("cfg") == "{\"c\":3}" && entries(dir) == 1, "the next save replaces the leftover temp file");

  FileStore missing(root + "/no/such/parent");
  missing.save("cfg", "{}");
  check(missing.load("cfg").empty(), "a save that cannot reach the volume fails without a crash");

  settingsBind(&fs);
  Settings& s = settings();
  s.volume = 77;
  s.ledMode = 2;
  s.deviceName = "Desk";
  s.rememberWifi("home", "secret");
  HostEntry h;
  h.id = "abcd1234";
  h.name = "laptop";
  h.pk[0] = 0x5a;
  s.upsertHost(h);
  s.leds[3].color = 19;
  s.bluetooth = true;
  settingsSave();
  check(bluetoothAtBoot(fs.load("cfg")), "the boot gate sees Bluetooth on in a saved file");
  s = Settings();
  settingsLoad();
  check(s.volume == 77 && s.ledMode == 2 && s.deviceName == "Desk" && s.bluetooth, "settings load back from the file");
  check(s.wifi.size() == 1 && s.wifi[0].pass == "secret" && s.hosts.size() == 1 && s.hosts[0].pk[0] == 0x5a,
        "Wi-Fi networks and paired hosts load back");
  check(s.leds[3].color == 19, "LED effects load back");
  s.bluetooth = false;
  settingsSave();
  check(!bluetoothAtBoot(fs.load("cfg")), "the boot gate sees Bluetooth off");
  s.blePairAtBoot = true;
  settingsSave();
  check(bluetoothAtBoot(fs.load("cfg")), "the boot gate keeps Bluetooth memory for a pairing restart");

  fs.wipe();
  check(fs.load("cfg").empty() && entries(dir) == -1, "wipe removes the files and the folder");
  fs.save("cfg", "{}");
  check(fs.load("cfg") == "{}", "saving works again after a wipe");

  fs.wipe();
  rmdir(root.c_str());
  printf("storecheck: %d failures\n", failures);
  return failures ? 1 : 0;
}
