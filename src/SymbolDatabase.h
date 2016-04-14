#pragma once

#include <iostream>
#include <set>
#include <string>
#include <unordered_map>

enum class SymbolType {
  function,
  variable,
  inconsistent,
};

struct SymbolLocation {
  SymbolType type;
  std::string filename;
  unsigned line_number;

  auto tie() const {
    return std::tie(filename, line_number);
  }

  bool operator<(const SymbolLocation& other) const {
    return tie() < other.tie();
  }

  bool operator==(const SymbolLocation& other) const {
    return tie() == other.tie();
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

  void dump(std::ostream& out) const {
    out << "    " << name << " declared in " << locations.size() << " locations:\n";
    for (auto location : locations) {
      const char* var_type = (location.type == SymbolType::function) ? "function" : "variable";
      out << "        " << var_type << " @ " << location.filename << ":" << location.line_number
          << "\n";
    }
  }
};

namespace clang {
class ASTUnit;
}

class SymbolDatabase {
 public:
  std::unordered_map<std::string, Symbol> symbols;

  void parseAST(clang::ASTUnit* ast);

  void dump(std::ostream& out = std::cout) const {
    out << "SymbolDatabase contains " << symbols.size() << " symbols:\n";
    for (const auto& pair : symbols) {
      pair.second.dump(out);
    }
  }
};
