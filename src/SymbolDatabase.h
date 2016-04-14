#pragma once

#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "android-base/strings.h"

enum class SymbolType {
  function,
  variable,
  inconsistent,
};

struct SymbolLocation {
  std::string filename;
  unsigned line_number;
  unsigned column;
  SymbolType type;
  mutable std::set<int> api_levels;

  auto tie() const {
    return std::tie(filename, line_number, column, type);
  }

  bool operator<(const SymbolLocation& other) const {
    return tie() < other.tie();
  }

  bool operator==(const SymbolLocation& other) const {
    return tie() == other.tie();
  }

  void addAPI(int api_level) const {
    api_levels.insert(api_level);
  }

  bool matchesAPI(int api_level) const {
    return api_levels.find(api_level) != api_levels.end();
  }
};

struct Symbol {
  std::string name;
  std::set<SymbolLocation> locations;

  SymbolType type() const {
    SymbolType result = locations.begin()->type;
    for (const SymbolLocation& location : locations) {
      if (location.type != result) {
        result = SymbolType::inconsistent;
      }
    }
    return result;
  }

  void dump(std::ostream& out = std::cout) const {
    out << "    " << name << " declared in " << locations.size() << " locations:\n";
    for (const SymbolLocation& location : locations) {
      const char* var_type = (location.type == SymbolType::function) ? "function" : "variable";
      std::string api_levels = android::base::Join(location.api_levels, ", ");
      out << "        " << var_type << " @ " << location.filename << ":" << location.line_number
          << ":" << location.column << " [" << api_levels << "]\n";
    }
  }
};

namespace clang {
class ASTUnit;
}

class SymbolDatabase {
 public:
  std::unordered_map<std::string, Symbol> symbols;

  void parseAST(clang::ASTUnit* ast, int api_level);

  void dump(std::ostream& out = std::cout) const {
    out << "SymbolDatabase contains " << symbols.size() << " symbols:\n";
    for (const auto& pair : symbols) {
      pair.second.dump(out);
    }
  }
};
