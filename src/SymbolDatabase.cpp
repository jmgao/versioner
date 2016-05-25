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

#include "SymbolDatabase.h"

#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <unordered_set>

#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"

#include "versioner.h"

using namespace llvm;
using namespace llvm::object;

std::unordered_set<std::string> getSymbols(const std::string& filename) {
  std::unordered_set<std::string> result;
  auto binary = createBinary(filename);
  if (std::error_code ec = binary.getError()) {
    errx(1, "failed to open library at %s: %s\n", filename.c_str(), ec.message().c_str());
    abort();
  }

  ELFObjectFileBase* elf = dyn_cast_or_null<ELFObjectFileBase>(binary.get().getBinary());
  if (!elf) {
    errx(1, "failed to parse %s as ELF", filename.c_str());
  }

  for (const ELFSymbolRef symbol : elf->getDynamicSymbolIterators()) {
    ErrorOr<StringRef> symbol_name = symbol.getName();

    if (std::error_code ec = binary.getError()) {
      errx(1, "failed to get symbol name for symbol in %s: %s", filename.c_str(),
           ec.message().c_str());
    }

    result.insert(symbol_name.get().str());
  }

  return result;
}

// The NDK platforms are built by copying the platform directories on top of
// each other to build each successive API version. Thus, we need to walk
// backwards to find each desired file.
static FILE* findFile(const CompilationType& type, const std::string& platform_dir,
                      const std::string& filename) {
  int api_level = type.api_level;
  while (true) {
    if (api_level < arch_min_api[type.arch]) {
      return nullptr;
    }

    if (supported_levels.count(api_level) == 0) {
      --api_level;
      continue;
    }

    std::string path = std::string(platform_dir) + "/android-" + std::to_string(api_level) +
                       "/arch-" + type.arch + "/symbols/" + filename;

    FILE* file = fopen(path.c_str(), "r");

    if (file) {
      return file;
    }

    --api_level;
  }
}

static std::map<std::string, NdkSymbolType> parsePlatform(const CompilationType& type,
                                                          const std::string& platform_dir) {
  std::map<std::string, NdkSymbolType> result;
  std::set<std::string> wanted_files = {
    "libc.so.functions.txt",
    "libc.so.variables.txt",
    "libdl.so.functions.txt",
    "libm.so.functions.txt",
    "libm.so.variables.txt",
  };

  for (const std::string& file : wanted_files) {
    NdkSymbolType symbol_type;
    if (EndsWith(file, ".functions.txt")) {
      symbol_type = NdkSymbolType::function;
    } else if (EndsWith(file, ".variables.txt")) {
      symbol_type = NdkSymbolType::variable;
    }

    FILE* f = findFile(type, platform_dir, file);
    if (!f) {
      err(1, "failed to find %s platform file '%s'", type.describe().c_str(), file.c_str());
    }

    while (true) {
      char* line = nullptr;
      size_t len = 0;
      ssize_t rc = getline(&line, &len, f);

      if (rc < 0) {
        break;
      }

      std::string symbol_name = Trim(line);
      free(line);

      if (symbol_name.empty()) {
        continue;
      }

      if (result.count(symbol_name) != 0) {
        if (verbose) {
          printf("duplicated symbol '%s' in '%s'\n", symbol_name.c_str(), file.c_str());
        }
      }

      result[symbol_name] = symbol_type;
    }

    fclose(f);
  }

  return result;
}

NdkSymbolDatabase parsePlatforms(const std::set<CompilationType>& types,
                                 const std::string& platform_dir) {
  std::map<std::string, std::map<CompilationType, NdkSymbolType>> result;
  for (const CompilationType& type : types) {
    std::map<std::string, NdkSymbolType> symbols = parsePlatform(type, platform_dir);
    for (auto& it : symbols) {
      result[it.first][type] = it.second;
    }
  }

  return result;
}
