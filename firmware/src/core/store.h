#pragma once
#include "platform.h"
#include <ArduinoJson.h>

struct Agent {
  std::string id, name, harness, backend, status, cwd, last;
  uint32_t age = 0;
  bool attn = false;
};

struct Mode {
  std::string id, label;
};

struct Harness {
  std::string id, name, backend, mode;
  bool ok = false;
  uint16_t color = 0xFFFF;
  std::vector<Mode> modes;
};

struct Project {
  std::string path, name, branch;
};

struct Proc {
  int pid = 0;
  std::string name, user;
  float cpu = 0, mem = 0;
};

struct Stats {
  bool valid = false;
  float cpu = 0;
  std::vector<float> cores;
  float load[3] = {0, 0, 0};
  uint32_t mem[2] = {0, 0}, swap[2] = {0, 0};
  float disk[2] = {0, 0};
  uint32_t net[2] = {0, 0};
  uint32_t up = 0, procs = 0;
  int battPct = -1;
  bool battChg = false;
  uint16_t working = 0, idle = 0, waiting = 0;
  uint32_t at = 0;
};

struct Notice {
  std::string kind, title, body, agent;
  uint32_t at = 0;
};

static constexpr int HIST = 64;

class Store {
 public:
  Stats stats;
  float cpuHist[HIST] = {0}, memHist[HIST] = {0};
  int histLen = 0, histPos = 0;
  std::vector<Agent> agents;
  uint32_t agentsRev = 0;
  std::vector<Harness> harness;
  uint32_t harnessAt = 0;
  std::vector<Project> projects;
  uint32_t projectsAt = 0;
  std::vector<Notice> notices;
  uint16_t unread = 0;

  void applyStats(JsonVariantConst d);
  void applyAgents(JsonVariantConst d);
  void applyHarness(JsonVariantConst d);
  void applyProjects(JsonVariantConst d);
  void addNotice(const Notice& n);
  void clearHost();

  const Agent* agent(const std::string& id) const;
  const Harness* findHarness(const std::string& id) const;
  int countStatus(const char* status) const;
};

Store& store();

uint16_t statusColor(const std::string& status);
uint16_t harnessColor(const std::string& harnessId);
std::string harnessShort(const std::string& harnessId);
std::string baseName(const std::string& path);
std::string fmtAge(uint32_t s);
std::string fmtMiB(uint32_t mib);
std::string fmtRate(uint32_t kib);
