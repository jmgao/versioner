#pragma once

#include <string>
#include <vector>

std::string getWorkingDir();
std::vector<std::string> collectFiles(const char* directory);

namespace std {
  static __attribute__((unused)) std::string to_string(const char* c) {
    return c;
  }

  static __attribute__((unused)) const std::string to_string(const std::string& str) {
    return str;
  }
}

template <typename Collection>
static std::string Join(Collection c, const std::string& delimiter = ", ") {
  std::string result;
  for (auto item : c) {
    result.append(std::to_string(item));
    result.append(delimiter);
  }
  if (!result.empty()) {
    result.resize(result.length() - delimiter.length());
  }
  return result;
}
