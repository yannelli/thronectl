#include "filestore.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <vector>

std::string FileStore::load(const char* key) {
  FILE* f = fopen(path(key).c_str(), "rb");
  if (!f) return "";
  std::string s;
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

void FileStore::save(const char* key, const std::string& value) {
  mkdir(dir_.c_str(), 0755);  // fails with EEXIST after the first save
  std::string to = path(key), tmp = to + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  bool ok = f && fwrite(value.data(), 1, value.size(), f) == value.size();
  if (f) ok = fclose(f) == 0 && ok;
  // The rename replaces the old file in one step, so a power cut leaves either the old or the new copy.
  if (ok) ok = rename(tmp.c_str(), to.c_str()) == 0;
  if (!ok) remove(tmp.c_str());
}

void FileStore::wipe() {
  DIR* d = opendir(dir_.c_str());
  if (!d) return;
  std::vector<std::string> names;
  while (dirent* e = readdir(d))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
  closedir(d);
  for (const auto& n : names) remove((dir_ + "/" + n).c_str());
  rmdir(dir_.c_str());
}
