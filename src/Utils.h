#pragma once

#include <string>
#include <vector>

std::string getWorkingDir();
std::vector<std::string> collectFiles(const char* directory);

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
