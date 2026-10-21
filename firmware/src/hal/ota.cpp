#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <esp_partition.h>
#include "hal.h"

// Cardputer ADV microSD. The display has SPI3, so the card gets the default SPI (FSPI).
static constexpr int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;
static constexpr uint8_t IMAGE_MAGIC = 0xE9;

static std::string lower(std::string s) {
  for (char& ch : s) ch = (char)tolower((unsigned char)ch);
  return s;
}

static std::string hex(const uint8_t* d, size_t n) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += digits[d[i] >> 4];
    s += digits[d[i] & 15];
  }
  return s;
}

bool OtaHal::setErr(const std::string& e) {
  err_ = e;
  return false;
}

uint32_t OtaHal::openFail(const std::string& e) {
  closeSd();
  err_ = e;
  return 0;
}

bool OtaHal::underLauncher() const {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, nullptr) != nullptr;
}

bool OtaHal::sdReady() {
  if (in_ || out_) return mounted_;
  // Mount again each time: the card may have been swapped since the last look.
  if (mounted_) SD.end();
  if (!spiBegun_) {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    spiBegun_ = true;
  }
  mounted_ = SD.begin(SD_CS, SPI, 25000000) && SD.cardType() != CARD_NONE;
  return mounted_;
}

std::vector<SdImage> OtaHal::sdImages() {
  std::vector<SdImage> out;
  if (!sdReady()) return out;
  for (const char* dir : {"/", "/thronectl"}) {
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) continue;
    for (File f = d.openNextFile(); f && out.size() < 40; f = d.openNextFile()) {
      std::string name = f.name();
      std::string l = lower(name);
      if (f.isDirectory() || name.empty() || name[0] == '.' || l.size() < 5 || l.compare(l.size() - 4, 4, ".bin")) continue;
      out.push_back({f.path(), name, (uint32_t)f.size()});
    }
  }
  return out;
}

bool OtaHal::begin(uint32_t size, const std::string& sha256, const std::string& sdPath) {
  abort();
  err_.clear();
  sha_ = lower(sha256);
  sdPath_ = sdPath;
  written_ = 0;
  if (!sdPath.empty()) {
    if (!sdReady()) return setErr("No SD card in the device");
    std::string part = sdPath + ".part";
    SD.remove(part.c_str());
    out_ = SD.open(part.c_str(), FILE_WRITE);
    if (!out_) return setErr("Could not create a file on the SD card");
    sink_ = Sink::Sd;
  } else {
    if (underLauncher()) return setErr("Launcher manages the app slots: install from the SD card in Launcher");
    if (!Update.begin(size, U_FLASH)) return setErr(std::string("Cannot start the update: ") + Update.errorString());
    sink_ = Sink::Flash;
  }
  hash_.reset(new tcrypto::Sha256());
  return true;
}

bool OtaHal::write(const uint8_t* data, size_t len) {
  if (sink_ == Sink::None) return setErr("No update in progress");
  if (!written_ && len && data[0] != IMAGE_MAGIC) return setErr("Not an ESP32 firmware image");
  hash_->update(data, len);
  written_ += len;
  if (sink_ == Sink::Sd) {
    if (out_.write(data, len) != len) return setErr("SD card write failed. Is it full?");
  } else if (Update.write(const_cast<uint8_t*>(data), len) != len) {
    return setErr(std::string("Flash write failed: ") + Update.errorString());
  }
  return true;
}

bool OtaHal::finish() {
  if (sink_ == Sink::None) return setErr("No update in progress");
  if (!sha_.empty()) {
    uint8_t d[32];
    hash_->finish(d);
    if (hex(d, 32) != sha_) {
      abort();
      return setErr("Checksum mismatch: the image was damaged on the way");
    }
  }
  Sink s = sink_;
  sink_ = Sink::None;
  hash_.reset();
  if (s == Sink::Flash) {
    if (!Update.end()) return setErr(std::string("The new image did not check out: ") + Update.errorString());
    return true;
  }
  out_.close();
  std::string part = sdPath_ + ".part";
  SD.remove(sdPath_.c_str());
  if (!SD.rename(part.c_str(), sdPath_.c_str())) return setErr("Could not rename the file on the SD card");
  return true;
}

void OtaHal::abort() {
  if (sink_ == Sink::Flash) Update.abort();
  if (sink_ == Sink::Sd) {
    out_.close();
    SD.remove((sdPath_ + ".part").c_str());
  }
  sink_ = Sink::None;
  hash_.reset();
}

uint32_t OtaHal::openSd(const std::string& path) {
  closeSd();
  err_.clear();
  if (!sdReady()) return openFail("No SD card in the device");
  in_ = SD.open(path.c_str(), FILE_READ);
  if (!in_) return openFail("Could not open " + path);
  uint32_t total = (uint32_t)in_.size(), start = 0;
  uint8_t e[32];
  if (in_.read(e, 1) != 1 || e[0] != IMAGE_MAGIC) return openFail("Not an ESP32 firmware image");
  // Merged image: bootloader at 0x0 and a partition table at 0x8000 (32-byte entries, magic AA 50).
  if (total > 0x8020 && in_.seek(0x8000) && in_.read(e, 32) == 32 && e[0] == 0xAA && e[1] == 0x50 && e[2] <= 1) {
    for (uint32_t off = 0x8000; off < 0x8C00 && e[0] == 0xAA && e[1] == 0x50; off += 32) {
      if (e[2] == 0) {
        start = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24;
        break;
      }
      if (!in_.seek(off + 32) || in_.read(e, 32) != 32) break;
    }
    if (!start) return openFail("The merged image has no app partition");
  }
  if (start >= total || !in_.seek(start) || in_.read(e, 1) != 1 || e[0] != IMAGE_MAGIC) return openFail("No app found in the image");
  in_.seek(start);
  return total - start;
}

int OtaHal::readSd(uint8_t* buf, size_t len) {
  return in_ ? (int)in_.read(buf, len) : -1;
}

void OtaHal::closeSd() {
  if (in_) in_.close();
}
