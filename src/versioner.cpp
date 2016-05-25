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

#include <dirent.h>
#include <err.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "clang/Tooling/Tooling.h"

#include "DeclarationDatabase.h"
#include "SymbolDatabase.h"
#include "Utils.h"
#include "versioner.h"

using namespace std::string_literals;
using namespace clang::tooling;

bool verbose;

class HeaderCompilationDatabase : public CompilationDatabase {
  CompilationType type;
  std::string cwd;
  std::vector<std::string> headers;
  std::vector<std::string> include_dirs;

 public:
  HeaderCompilationDatabase(CompilationType type, std::string cwd, std::vector<std::string> headers,
                            std::vector<std::string> include_dirs)
      : type(type),
        cwd(std::move(cwd)),
        headers(std::move(headers)),
        include_dirs(std::move(include_dirs)) {
  }

  CompileCommand generateCompileCommand(const std::string& filename) const {
    std::vector<std::string> command = { "clang-tool", filename, "-nostdlibinc" };
    for (const auto& dir : include_dirs) {
      command.push_back("-isystem");
      command.push_back(dir);
    }
    command.push_back("-std=c11");
    command.push_back("-DANDROID");
    command.push_back("-D__ANDROID_API__="s + std::to_string(type.api_level));
    command.push_back("-D_FORTIFY_SOURCE=2");
    command.push_back("-D_GNU_SOURCE");
    command.push_back("-Wno-unknown-attributes");
    command.push_back("-target");
    command.push_back(arch_targets[type.arch]);

    return CompileCommand(cwd, filename, command);
  }

  std::vector<CompileCommand> getAllCompileCommands() const override {
    std::vector<CompileCommand> commands;
    for (const std::string& file : headers) {
      commands.push_back(generateCompileCommand(file));
    }
    return commands;
  }

  std::vector<CompileCommand> getCompileCommands(StringRef file) const override {
    std::vector<CompileCommand> commands;
    commands.push_back(generateCompileCommand(file));
    return commands;
  }

  std::vector<std::string> getAllFiles() const override {
    return headers;
  }
};

struct CompilationRequirements {
  std::vector<std::string> headers;
  std::vector<std::string> dependencies;
};

static CompilationRequirements collectRequirements(const std::string& arch,
                                                   const std::string& header_dir,
                                                   const std::string& dependency_dir) {
  std::vector<std::string> headers = collectFiles(header_dir);

  std::vector<std::string> dependencies = { header_dir };
  if (!dependency_dir.empty()) {
    auto collect_children = [&dependencies](const std::string& dir_path) {
      DIR* dir = opendir(dir_path.c_str());
      if (!dir) {
        err(1, "failed to open dependency dir");
      }

      struct dirent* dent;
      while ((dent = readdir(dir))) {
        if (dent->d_name[0] == '.') {
          continue;
        }

        // TODO: Resolve symlinks.
        std::string dependency = dir_path + "/" + dent->d_name;
        dependencies.push_back(dependency);
      }

      closedir(dir);
    };

    collect_children(dependency_dir + "/common");
    collect_children(dependency_dir + "/" + arch);
  }

  auto new_end = std::remove_if(headers.begin(), headers.end(), [&arch](const std::string& header) {
    for (const auto& it : header_blacklist) {
      if (it.second.find(arch) == it.second.end()) {
        continue;
      }

      if (EndsWith(header, "/" + it.first)) {
        return true;
      }
    }
    return false;
  });

  headers.erase(new_end, headers.end());

  CompilationRequirements result = { .headers = headers, .dependencies = dependencies };
  return result;
}

static std::set<CompilationType> generateCompilationTypes(
  const std::set<std::string> selected_architectures, const std::set<int>& selected_levels) {
  std::set<CompilationType> result;
  for (const std::string& arch : selected_architectures) {
    int min_api = arch_min_api[arch];
    for (int api_level : selected_levels) {
      if (api_level < min_api) {
        continue;
      }
      CompilationType type = { .arch = arch, .api_level = api_level };
      result.insert(type);
    }
  }
  return result;
}

using DeclarationDatabase = std::map<std::string, std::map<CompilationType, Declaration>>;

static DeclarationDatabase transposeHeaderDatabases(
  const std::map<CompilationType, HeaderDatabase>& original) {
  DeclarationDatabase result;
  for (const auto& outer : original) {
    const CompilationType& type = outer.first;
    for (const auto& inner : outer.second.declarations) {
      const std::string& symbol_name = inner.first;
      result[symbol_name][type] = inner.second;
    }
  }
  return result;
}

static DeclarationDatabase compileHeaders(const std::set<CompilationType>& types,
                                          const std::string& header_dir,
                                          const std::string& dependency_dir) {
  constexpr size_t thread_count = 8;
  size_t threads_created = 0;
  std::mutex mutex;
  std::vector<std::thread> threads(thread_count);

  std::map<CompilationType, HeaderDatabase> header_databases;
  std::unordered_map<std::string, CompilationRequirements> requirements;

  std::string cwd = getWorkingDir();

  for (const auto& arch : supported_archs) {
    requirements[arch] = collectRequirements(arch, header_dir, dependency_dir);
  }

  for (CompilationType type : types) {
    size_t thread_id = threads_created++;
    if (thread_id >= thread_count) {
      thread_id = thread_id % thread_count;
      threads[thread_id].join();
    }

    threads[thread_id] = std::thread(
      [&](CompilationType type) {
        const auto& req = requirements[type.arch];

        HeaderDatabase database;
        HeaderCompilationDatabase compilationDatabase(type, cwd, req.headers, req.dependencies);
        ClangTool tool(compilationDatabase, req.headers);

        std::vector<std::unique_ptr<clang::ASTUnit>> asts;
        tool.buildASTs(asts);
        for (const auto& ast : asts) {
          database.parseAST(ast.get());
        }

        std::unique_lock<std::mutex> l(mutex);
        header_databases[type] = database;
      },
      type);
  }

  if (threads_created < thread_count) {
    threads.resize(threads_created);
  }

  for (auto& thread : threads) {
    thread.join();
  }

  return transposeHeaderDatabases(header_databases);
}

static bool sanityCheck(const std::set<CompilationType>& types,
                        const DeclarationDatabase& database) {
  bool error = false;
  for (auto outer : database) {
    const std::string& symbol_name = outer.first;
    CompilationType last_type;
    DeclarationAvailability last_availability;

    for (CompilationType type : types) {
      auto inner = outer.second.find(type);
      if (inner == outer.second.end()) {
        // TODO: Check for holes.
        continue;
      }

      const Declaration& declaration = inner->second;
      bool found_availability = false;
      bool availability_mismatch = false;
      DeclarationAvailability current_availability;

      // Make sure that all of the availability declarations for this symbol match.
      for (const DeclarationLocation& location : declaration.locations) {
        if (!found_availability) {
          found_availability = true;
          current_availability = location.availability;
          continue;
        }

        if (current_availability != location.availability) {
          availability_mismatch = true;
          error = true;
        }
      }

      if (availability_mismatch) {
        printf("%s: availability mismatch for %s\n", symbol_name.c_str(), type.describe().c_str());
        declaration.dump(getWorkingDir() + "/");
      }

      if (type.arch != last_type.arch) {
        last_type = type;
        last_availability = current_availability;
        continue;
      }

      // Make sure that availability declarations are consistent across API levels for a given arch.
      if (last_availability != current_availability) {
        error = true;
        printf("%s: availability mismatch between %s and %s: %s before, %s after\n",
               symbol_name.c_str(), last_type.describe().c_str(), type.describe().c_str(),
               last_availability.describe().c_str(), current_availability.describe().c_str());
      }

      last_type = type;
    }
  }
  return !error;
}

static bool checkVersions(const std::set<CompilationType>& compilation_types,
                          const DeclarationDatabase& declaration_database,
                          const NdkSymbolDatabase& symbol_database) {
  bool failed = false;

  // Map from symbol name to a map from arch to availability.
  std::map<std::string, std::map<std::string, Declaration>> symbol_availability;

  for (const auto& outer : declaration_database) {
    const std::string& symbol_name = outer.first;
    const std::map<CompilationType, Declaration>& declarations = outer.second;

    for (const auto& inner : declarations) {
      std::map<std::string, Declaration>& arch_map = symbol_availability[symbol_name];

      if (arch_map.count(inner.first.arch) == 0) {
        arch_map[inner.first.arch] = inner.second;
      }
    }
  }

  for (const auto& outer : symbol_availability) {
    const std::string& symbol_name = outer.first;
    const std::map<std::string, Declaration>& arch_availability = outer.second;

    std::set<std::string> missing_types;
    size_t total_types = 0;
    for (const auto& inner : arch_availability) {
      const std::string& arch = inner.first;
      const Declaration& declaration = inner.second;
      for (int api_level : supported_levels) {
        if (api_level < arch_min_api[arch]) {
          continue;
        }

        const DeclarationAvailability& availability = declaration.locations.begin()->availability;
        if (availability.introduced != 0 && api_level < availability.introduced) {
          continue;
        } else if (availability.obsoleted != 0 && api_level >= availability.obsoleted) {
          continue;
        }

        ++total_types;

        CompilationType type = { .arch = inner.first, .api_level = api_level };
        type.api_level = api_level;

        auto symbol_it = symbol_database.find(symbol_name);
        if (symbol_it == symbol_database.end()) {
          if (verbose) {
            printf("%s: not available in any platform\n", symbol_name.c_str());
            failed = true;
          }
          break;
        }

        const std::map<CompilationType, NdkSymbolType>& symbol_availability = symbol_it->second;
        auto availability_it = symbol_availability.find(type);

        if (availability_it == symbol_availability.end()) {
          // Check to see if the symbol exists as an inline definition.
          CompilationType type = { .arch = arch, .api_level = api_level };

          const auto& declaration_map = declaration_database.find(symbol_name)->second;
          auto it = declaration_map.find(type);
          if (it == declaration_map.end()) {
            printf("%s: symbol not available in %s\n", symbol_name.c_str(), type.describe().c_str());
            continue;
          }

          if (!it->second.hasDefinition()) {
            missing_types.insert(type.describe());
            failed = true;
            continue;
          }
        }

        switch (availability_it->second) {
          case NdkSymbolType::function:
            if (declaration.type() != DeclarationType::function) {
              printf("%s: symbol exists as function, declared as %s\n", symbol_name.c_str(),
                     declarationTypeName(declaration.type()));
            }
            break;

          case NdkSymbolType::variable:
            if (declaration.type() != DeclarationType::variable) {
              printf("%s: symbol exists as function, declared as %s\n", symbol_name.c_str(),
                     declarationTypeName(declaration.type()));
            }
            break;
        }
      }
    }

    if (!missing_types.empty()) {
      // If the symbol is missing everywhere, only warn if verbose.
      if (missing_types.size() != total_types || verbose) {
        printf("%s: missing in [%s]\n", symbol_name.c_str(), Join(missing_types, ", ").c_str());
      }
    }
  }

  using AvailabilityMismatch =
    std::tuple<std::string, unsigned int, std::string, std::string, std::string>;
  std::set<AvailabilityMismatch> mismatches;

  // Make sure that we expose declarations for all available versions.
  for (const auto& outer : symbol_database) {
    const std::string& symbol_name = outer.first;
    std::set<std::string> warned_archs;

    auto decl_it = declaration_database.find(symbol_name);
    if (decl_it == declaration_database.end()) {
      // It's okay for a symbol to not be declared at all.
      continue;
    }

    for (const auto& inner : outer.second) {
      const CompilationType& type = inner.first;
      auto symbol_it = decl_it->second.find(type);
      if (symbol_it == decl_it->second.end()) {
        printf("%s: failed to find declaration for %s\n", symbol_name.c_str(),
               type.describe().c_str());
        failed = true;
        continue;
      }

      DeclarationAvailability availability = symbol_it->second.locations.begin()->availability;
      if ((availability.introduced > 0 && availability.introduced > type.api_level) ||
          (availability.obsoleted > 0 && availability.obsoleted <= type.api_level)) {
        if (warned_archs.count(type.arch)) {
          continue;
        }

        DeclarationLocation location =
          *declaration_database.find(symbol_name)->second.find(type)->second.locations.begin();

        mismatches.emplace(location.filename, location.line_number, symbol_name.c_str(),
                           type.describe(), availability.describe());
        warned_archs.insert(type.arch);
        failed = true;
      }
    }
  }

  for (const auto& mismatch : mismatches) {
    const std::string& filename = std::get<0>(mismatch);
    const unsigned int line_number = std::get<1>(mismatch);
    const std::string& symbol_name = std::get<2>(mismatch);
    const std::string& arch = std::get<3>(mismatch);
    const std::string& availability = std::get<4>(mismatch);
    printf("%s: available in %s, but availability declared as %s (at %s:%u)\n", symbol_name.c_str(),
           arch.c_str(), availability.c_str(), filename.c_str(), line_number);
  }

  return !failed;
}

static void usage() {
  fprintf(stderr, "Usage: versioner [OPTION]... HEADER_PATH [DEPS_PATH]\n");
  fprintf(stderr, "Version headers at HEADER_PATH, with DEPS_PATH/* on the include path\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Target specification (defaults to all):\n");
  fprintf(stderr, "  -a API_LEVEL\tbuild with specified API level (can be repeated)\n");
  fprintf(stderr, "    \t\tvalid levels are %s\n", Join(supported_levels).c_str());
  fprintf(stderr, "  -r ARCH\tbuild with specified architecture (can be repeated)\n");
  fprintf(stderr, "    \t\tvalid architectures are %s\n", Join(supported_archs).c_str());
  fprintf(stderr, "\n");
  fprintf(stderr, "Validation:\n");
  fprintf(stderr, "  -p PLATFORM_PATH\tcompare against NDK platform at PLATFORM_PATH\n");
  fprintf(stderr, "  -d\t\tdump symbol availability in libraries\n");
  fprintf(stderr, "  -v\t\tenable verbose warnings\n");
  exit(1);
}

int main(int argc, char** argv) {
  std::string cwd = getWorkingDir() + "/";
  bool default_args = true;
  std::string platform_dir;
  std::set<std::string> selected_architectures;
  std::set<int> selected_levels;

  int c;
  while ((c = getopt(argc, argv, "a:r:p:n:duv")) != -1) {
    default_args = false;
    switch (c) {
      case 'a': {
        char* end;
        int api_level = strtol(optarg, &end, 10);
        if (end == optarg || strlen(end) > 0) {
          usage();
        }

        if (supported_levels.count(api_level) == 0) {
          errx(1, "unsupported API level %d", api_level);
        }

        selected_levels.insert(api_level);
        break;
      }

      case 'r': {
        if (supported_archs.count(optarg) == 0) {
          errx(1, "unsupported architecture: %s", optarg);
        }
        selected_architectures.insert(optarg);
        break;
      }

      case 'p': {
        if (!platform_dir.empty()) {
          usage();
        }

        platform_dir = optarg;

        struct stat st;
        if (stat(platform_dir.c_str(), &st) != 0) {
          err(1, "failed to stat platform directory '%s'", platform_dir.c_str());
        }
        if (!S_ISDIR(st.st_mode)) {
          errx(1, "%s is not a directory", optarg);
        }
        break;
      }

      case 'v':
        verbose = true;
        break;

      default:
        usage();
        break;
    }
  }

  if (argc - optind > 2 || optind >= argc) {
    usage();
  }

  if (selected_levels.empty()) {
    selected_levels = supported_levels;
  }

  if (selected_architectures.empty()) {
    selected_architectures = supported_archs;
  }

  std::string dependencies = (argc - optind == 2) ? argv[optind + 1] : "";
  std::set<CompilationType> compilation_types;
  DeclarationDatabase declaration_database;
  NdkSymbolDatabase symbol_database;

  compilation_types = generateCompilationTypes(selected_architectures, selected_levels);

  // Do this before compiling so that we can early exit if the platforms don't match what we expect.
  if (!platform_dir.empty()) {
    symbol_database = parsePlatforms(compilation_types, platform_dir);
  }

  declaration_database = compileHeaders(compilation_types, argv[optind], dependencies);

  if (!sanityCheck(compilation_types, declaration_database)) {
    return 1;
  }

  if (!platform_dir.empty()) {
    if (!checkVersions(compilation_types, declaration_database, symbol_database)) {
      return 1;
    }
  }

  return 0;
}
