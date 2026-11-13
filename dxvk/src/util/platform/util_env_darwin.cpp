#include "util_env.h"
#include <array>
#include <filesystem>
#include <limits.h>
extern "C" uint32_t _NSGetExecutablePath(char* path, uint32_t* size);
namespace dxvk::env {
  std::string getExePath() {
    std::array<char, PATH_MAX> exePath = {};
    uint32_t count = exePath.size();
    _NSGetExecutablePath(exePath.data(), &count);
    return std::string(exePath.data());
  }
  void setThreadName(const std::string& name) { }
  bool createDirectory(const std::string& path) {
    return std::filesystem::create_directories(path);
  }
}
