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

static const char* symbolTypeName(SymbolType type) {
  switch (type) {
    case SymbolType::function:
      return "function";
    case SymbolType::variable:
      return "variable";
    case SymbolType::inconsistent:
      return "inconsistent";
  }
}

struct SymbolLocation {
  std::string filename;
  unsigned line_number;
  unsigned column;
  SymbolType type;
  bool is_extern;
  bool is_definition;
  mutable std::set<int> api_levels;

  auto tie() const {
    return std::tie(filename, line_number, column, type, is_extern, is_definition);
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

enum class SymbolDeclarationType {
  nonexistent,
  unique,
  inlined,
  multiply_declared,
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
      const char* var_type = symbolTypeName(location.type);
      const char* declaration_type = location.is_definition ? "definition" : "declaration";
      const char* linkage = location.is_extern ? "extern" : "static";
      std::string api_levels = android::base::Join(location.api_levels, ",");
      out << "        " << linkage << " " << var_type << " " << declaration_type << " @ "
          << location.filename << ":" << location.line_number << ":" << location.column << " ["
          << api_levels << "]\n";
    }
  }

  SymbolDeclarationType getDeclarationType(int api_level) const {
    int declarations = 0;
    int definitions = 0;
    for (const SymbolLocation& location : locations) {
      if (!location.matchesAPI(api_level)) {
        continue;
      }

      if (location.is_definition) {
        ++definitions;
      } else {
        ++declarations;
      }
    }

    if (definitions > 1) {
      fprintf(stderr, "ERROR: multiple definitions for symbol\n");
      dump();
      exit(1);
    }

    if (declarations > 1) {
      return SymbolDeclarationType::multiply_declared;
    } else if (definitions == 1) {
      return SymbolDeclarationType::inlined;
    } else if (declarations == 1) {
      return SymbolDeclarationType::unique;
    }

    return SymbolDeclarationType::nonexistent;
  }
};

namespace clang {
class ASTUnit;
}

class HeaderDatabase {
 public:
  std::unordered_map<std::string, Symbol> symbols;

  void parseAST(clang::ASTUnit* ast, int api_level);

  void dump(std::ostream& out = std::cout) const {
    out << "HeaderDatabase contains " << symbols.size() << " symbols:\n";
    for (const auto& pair : symbols) {
      pair.second.dump(out);
    }
  }
};
