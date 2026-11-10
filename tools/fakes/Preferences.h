#pragma once
// Host fake for exercising NVS slot selection under interrupted/corrupt writes.
#include <map>
#include <string>
#include <vector>
#include <cstring>
namespace FakeNvs {
extern std::map<std::string, std::vector<unsigned char>> values;
enum Fault { None, TornWrite, WriteFailure };
extern Fault fault;
extern bool openOk;
}
class Preferences {
 public:
  bool begin(const char *name, bool) { prefix_ = std::string(name) + "/"; return FakeNvs::openOk; }
  size_t getBytesLength(const char *name) { return FakeNvs::values[prefix_ + name].size(); }
  size_t getBytes(const char *name, void *out, size_t capacity) {
    const auto &v = FakeNvs::values[prefix_ + name];
    if (v.size() > capacity) return 0;
    if (!v.empty()) memcpy(out, v.data(), v.size());
    return v.size();
  }
  size_t putBytes(const char *name, const void *data, size_t size) {
    if (FakeNvs::fault == FakeNvs::WriteFailure) return 0;
    const auto *p = static_cast<const unsigned char *>(data);
    auto &v = FakeNvs::values[prefix_ + name]; v.assign(p, p + size);
    if (FakeNvs::fault == FakeNvs::TornWrite) v.resize(size / 2);
    return size; // caller must validate readback, not trust reported success
  }
 private:
  std::string prefix_;
};
