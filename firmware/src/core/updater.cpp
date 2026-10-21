#include "updater.h"

static Updater g_updater;
Updater& updater() { return g_updater; }

// Two reads in flight: the next chunk crosses the network while this one goes to flash.
static constexpr int WINDOW = 2;

void Updater::reset() {
  gen_++;
  phase_ = UpdatePhase::Idle;
  fromLink_ = toSd_ = false;
  size_ = off_ = asked_ = rebootAt_ = 0;
  inflight_ = 0;
  version_.clear();
  err_.clear();
  sdPath_.clear();
  std::vector<uint8_t>().swap(buf_);
}

void Updater::reply(bool ok, const std::string& err, const char* dest) {
  JsonDocument p;
  p["ok"] = ok;
  if (!ok) p["err"] = err;
  p["dest"] = dest;
  svc().link->call("ota.done", p, [](bool, JsonVariantConst, const char*) {});
}

void Updater::startLink(const std::string& version, uint32_t size, const std::string& sha256, bool toSd) {
  reset();
  phase_ = UpdatePhase::Running;
  fromLink_ = true;
  toSd_ = toSd;
  version_ = version;
  size_ = size;
  if (toSd) sdPath_ = "/thronectl-cardputer-adv-" + (version.empty() ? std::string("update") : "v" + version) + ".bin";
  if (!size) return fail("The host offered an empty image");
  if (!svc().ota->begin(size, sha256, sdPath_)) return fail(svc().ota->error());
  request();
}

void Updater::startSd(const std::string& path) {
  reset();
  phase_ = UpdatePhase::Running;
  sdPath_ = path;
  size_ = svc().ota->openSd(path);
  if (!size_) return fail(svc().ota->error());
  if (!svc().ota->begin(size_, "", "")) return fail(svc().ota->error());
  buf_.resize(CHUNK);
}

void Updater::request() {
  while (active() && inflight_ < WINDOW && asked_ < size_) {
    uint32_t at = asked_, n = size_ - asked_ < CHUNK ? size_ - asked_ : CHUNK;
    JsonDocument p;
    p["off"] = at;
    p["n"] = n;
    asked_ += n;
    inflight_++;
    uint32_t gen = gen_;
    // The host sends the bytes as a KIND_OTA frame just before this reply, so onData has run by now.
    svc().link->call("ota.read", p, [this, gen, at](bool ok, JsonVariantConst r, const char* err) {
      if (gen != gen_ || !active()) return;
      inflight_--;
      if (!ok) return fail(std::string("Transfer stopped: ") + (err ? err : "no reply"));
      uint32_t got = r["n"] | 0;
      if (!got || off_ < at + got) return fail("The host sent no data");
      request();
    }, 15000);
  }
}

void Updater::onData(const uint8_t* data, size_t len) {
  if (!active() || !fromLink_ || len < 4) return;
  uint32_t at = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3];
  uint32_t n = (uint32_t)(len - 4);
  if (at != off_ || n > size_ - off_) return fail("Data arrived out of order");
  if (!svc().ota->write(data + 4, n)) return fail(svc().ota->error());
  off_ += n;
  if (off_ == size_) complete();
}

void Updater::update(uint32_t now) {
  if (!active() || fromLink_) return;
  uint32_t want = size_ - off_ < CHUNK ? size_ - off_ : CHUNK;
  int n = svc().ota->readSd(buf_.data(), want);
  if (n <= 0) return fail("Could not read the SD card");
  if (!svc().ota->write(buf_.data(), (size_t)n)) return fail(svc().ota->error());
  off_ += (uint32_t)n;
  if (off_ >= size_) complete();
}

void Updater::cancel(const std::string& why) { fail(why); }

void Updater::complete() {
  if (!fromLink_) svc().ota->closeSd();
  if (!svc().ota->finish()) return fail(svc().ota->error());
  phase_ = UpdatePhase::Done;
  std::vector<uint8_t>().swap(buf_);
  if (fromLink_) reply(true, "", toSd_ ? "sd" : "flash");
  if (!toSd_) rebootAt_ = (nowMs() + 2500) | 1;
}

void Updater::fail(const std::string& why) {
  if (phase_ != UpdatePhase::Running) return;
  svc().ota->abort();
  if (!fromLink_) svc().ota->closeSd();
  phase_ = UpdatePhase::Failed;
  err_ = why.empty() ? "Update failed" : why;
  std::vector<uint8_t>().swap(buf_);
  if (fromLink_) reply(false, err_, toSd_ ? "sd" : "flash");
}
