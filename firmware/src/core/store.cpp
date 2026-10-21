#include "store.h"
#include "theme.h"

static Store g_store;
Store& store() { return g_store; }

static std::string s(JsonVariantConst v) {
  const char* p = v.as<const char*>();
  return p ? p : "";
}

void Store::applyStats(JsonVariantConst d) {
  Stats n;
  n.valid = true;
  n.cpu = d["cpu"] | 0.0f;
  for (JsonVariantConst c : d["cores"].as<JsonArrayConst>()) n.cores.push_back(c.as<float>());
  for (int i = 0; i < 3; i++) n.load[i] = d["load"][i] | 0.0f;
  for (int i = 0; i < 2; i++) {
    n.mem[i] = d["mem"][i] | 0u;
    n.swap[i] = d["swap"][i] | 0u;
    n.disk[i] = d["disk"][i] | 0.0f;
    n.net[i] = d["net"][i] | 0u;
  }
  n.up = d["up"] | 0u;
  n.procs = d["procs"] | 0u;
  if (!d["batt"].isNull()) {
    n.battPct = d["batt"]["pct"] | -1;
    n.battChg = d["batt"]["chg"] | false;
  }
  n.working = d["agents"]["working"] | 0;
  n.idle = d["agents"]["idle"] | 0;
  n.waiting = d["agents"]["waiting"] | 0;
  n.at = nowMs();
  stats = n;
  cpuHist[histPos] = n.cpu;
  memHist[histPos] = n.mem[1] ? 100.0f * n.mem[0] / n.mem[1] : 0;
  rxHist[histPos] = (float)n.net[0];
  txHist[histPos] = (float)n.net[1];
  histPos = (histPos + 1) % HIST;
  if (histLen < HIST) histLen++;
}

void Store::applyAgents(JsonVariantConst d) {
  std::vector<Agent> list;
  for (JsonObjectConst o : d["agents"].as<JsonArrayConst>()) {
    Agent a;
    a.id = s(o["id"]);
    a.name = s(o["name"]);
    a.harness = s(o["harness"]);
    a.backend = s(o["backend"]);
    a.status = s(o["status"]);
    a.cwd = s(o["cwd"]);
    a.last = s(o["last"]);
    a.age = o["age"] | 0u;
    a.attn = o["attn"] | false;
    list.push_back(std::move(a));
  }
  agents = std::move(list);
  agentsRev++;
}

void Store::applyHarness(JsonVariantConst d) {
  harness.clear();
  for (JsonObjectConst o : d["harness"].as<JsonArrayConst>()) {
    Harness h;
    h.id = s(o["id"]);
    h.name = s(o["name"]);
    h.backend = s(o["backend"]);
    h.ok = o["ok"] | false;
    h.color = o["color"] | 0xFFFF;
    h.mode = s(o["mode"]);
    for (JsonObjectConst m : o["modes"].as<JsonArrayConst>()) h.modes.push_back({s(m["id"]), s(m["label"])});
    harness.push_back(std::move(h));
  }
  harnessAt = nowMs();
}

void Store::applyProjects(JsonVariantConst d) {
  projects.clear();
  for (JsonObjectConst o : d["projects"].as<JsonArrayConst>()) projects.push_back({s(o["path"]), s(o["name"]), s(o["branch"])});
  projectsAt = nowMs();
}

void Store::addNotice(const Notice& n) {
  notices.insert(notices.begin(), n);
  if (notices.size() > 40) notices.pop_back();
  if (unread < 99) unread++;
}

void Store::clearHost() {
  stats = Stats();
  histLen = histPos = 0;
  agents.clear();
  agentsRev++;
  harness.clear();
  harnessAt = 0;
  projects.clear();
  projectsAt = 0;
}

const Agent* Store::agent(const std::string& id) const {
  for (auto& a : agents)
    if (a.id == id) return &a;
  return nullptr;
}

const Harness* Store::findHarness(const std::string& id) const {
  for (auto& h : harness)
    if (h.id == id) return &h;
  return nullptr;
}

int Store::countStatus(const char* status) const {
  int n = 0;
  for (auto& a : agents)
    if (a.status == status) n++;
  return n;
}

uint16_t statusColor(const std::string& st) {
  const Theme& t = T();
  if (st == "working") return t.accent;
  if (st == "waiting") return t.warn;
  if (st == "error") return t.err;
  if (st == "idle") return t.ok;
  return t.dim;
}

uint16_t harnessColor(const std::string& id) {
  if (const Harness* h = store().findHarness(id)) return h->color;
  std::string k = harnessShort(id);
  if (k == "claude") return rgb(0xD97757);
  if (k == "codex") return rgb(0x10A37F);
  if (k == "pi" || k == "pi-rust" || k == "omp") return rgb(0xC792EA);
  if (k == "cursor") return rgb(0x7FD1FF);
  if (k == "antigravity") return rgb(0x4285F4);
  if (k == "gemini") return rgb(0x8E75FF);
  return T().accent2;
}

std::string harnessShort(const std::string& id) {
  size_t p = id.find(':');
  return p == std::string::npos ? id : id.substr(p + 1);
}

std::string baseName(const std::string& path) {
  size_t end = path.size();
  while (end > 1 && path[end - 1] == '/') end--;
  size_t p = path.rfind('/', end - 1);
  return p == std::string::npos ? path.substr(0, end) : path.substr(p + 1, end - p - 1);
}

std::string fmtAge(uint32_t s) {
  char b[16];
  if (s < 60) snprintf(b, sizeof b, "%us", (unsigned)s);
  else if (s < 3600) snprintf(b, sizeof b, "%um", (unsigned)(s / 60));
  else if (s < 86400) snprintf(b, sizeof b, "%uh", (unsigned)(s / 3600));
  else snprintf(b, sizeof b, "%ud", (unsigned)(s / 86400));
  return b;
}

std::string fmtMiB(uint32_t mib) {
  char b[16];
  if (mib >= 10240) snprintf(b, sizeof b, "%.0fG", mib / 1024.0f);
  else if (mib >= 1024) snprintf(b, sizeof b, "%.1fG", mib / 1024.0f);
  else snprintf(b, sizeof b, "%uM", (unsigned)mib);
  return b;
}

std::string fmtRate(uint32_t kib) {
  char b[16];
  if (kib >= 1024) snprintf(b, sizeof b, "%.1fM", kib / 1024.0f);
  else snprintf(b, sizeof b, "%uK", (unsigned)kib);
  return b;
}
