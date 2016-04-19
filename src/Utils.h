#pragma once

#include <string>
#include <vector>

std::string getWorkingDir();
std::vector<std::string> collectFiles(const char* directory);
