/*
 * Copyright (C) 2016 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Utils.h"

enum class DeclarationType {
  function,
  variable,
  inconsistent,
};

static const char* declarationTypeName(DeclarationType type) {
  switch (type) {
    case DeclarationType::function:
      return "function";
    case DeclarationType::variable:
      return "variable";
    case DeclarationType::inconsistent:
      return "inconsistent";
  }
}

struct CompilationType {
  std::string arch;
  int api_level;

 private:
  auto tie() const {
    return std::make_tuple(arch, api_level);
  }

 public:
  bool operator<(const CompilationType& other) const {
    return tie() < other.tie();
  }

  bool operator==(const CompilationType& other) const {
    return tie() == other.tie();
  }

  std::string describe() const {
    return arch + "-" + std::to_string(api_level);
  }
};

struct DeclarationAvailability {
  int introduced = 0;
  int deprecated = 0;
  int obsoleted = 0;

  void dump(std::ostream& out = std::cout) const {
    bool need_comma = false;
    auto comma = [&out, &need_comma]() {
      if (!need_comma) {
        need_comma = true;
        return;
      }
      out << ", ";
    };

    if (introduced != 0) {
      comma();
      out << "introduced = " << introduced;
    }
    if (deprecated != 0) {
      comma();
      out << "deprecated = " << deprecated;
    }
    if (obsoleted != 0) {
      comma();
      out << "obsoleted = " << obsoleted;
    }
  }

  bool empty() const {
    return !(introduced || deprecated || obsoleted);
  }

  auto tie() const {
    return std::tie(introduced, deprecated, obsoleted);
  }

  bool operator==(const DeclarationAvailability& rhs) const {
    return this->tie() == rhs.tie();
  }

  bool operator!=(const DeclarationAvailability& rhs) const {
    return !(*this == rhs);
  }

  std::string describe() const {
    return std::string("[") + std::to_string(introduced) + "," + std::to_string(deprecated) + "," +
           std::to_string(obsoleted) + "]";
  }
};

struct DeclarationLocation {
  std::string filename;
  unsigned line_number;
  unsigned column;
  DeclarationType type;
  bool is_extern;
  bool is_definition;
  DeclarationAvailability availability;

  auto tie() const {
    return std::tie(filename, line_number, column, type, is_extern, is_definition);
  }

  bool operator<(const DeclarationLocation& other) const {
    return tie() < other.tie();
  }

  bool operator==(const DeclarationLocation& other) const {
    return tie() == other.tie();
  }
};

struct Declaration {
  std::string name;
  std::set<DeclarationLocation> locations;

  bool hasDefinition() const {
    for (const auto& location : locations) {
      if (location.is_definition) {
        return true;
      }
    }
    return false;
  }

  DeclarationType type() const {
    DeclarationType result = locations.begin()->type;
    for (const DeclarationLocation& location : locations) {
      if (location.type != result) {
        result = DeclarationType::inconsistent;
      }
    }
    return result;
  }

  void dump(const std::string& base_path = "", std::ostream& out = std::cout) const {
    out << "    " << name << " declared in " << locations.size() << " locations:\n";
    for (const DeclarationLocation& location : locations) {
      const char* var_type = declarationTypeName(location.type);
      const char* declaration_type = location.is_definition ? "definition" : "declaration";
      const char* linkage = location.is_extern ? "extern" : "static";

      std::string filename;
      if (StartsWith(location.filename, base_path)) {
        filename = location.filename.substr(base_path.length());
      } else {
        filename = location.filename;
      }

      out << "        " << linkage << " " << var_type << " " << declaration_type << " @ "
          << filename << ":" << location.line_number << ":" << location.column;

      if (!location.availability.empty()) {
        out << "\t[";
        location.availability.dump(out);
        out << "]";
      } else {
        out << "\t[no availability]";
      }

      out << "\n";
    }
  }
};

namespace clang {
class ASTUnit;
}

class HeaderDatabase {
 public:
  std::map<std::string, Declaration> declarations;

  void parseAST(clang::ASTUnit* ast);

  void dump(const std::string& base_path = "", std::ostream& out = std::cout) const {
    out << "HeaderDatabase contains " << declarations.size() << " declarations:\n";
    for (const auto& pair : declarations) {
      pair.second.dump(base_path, out);
    }
  }
};
