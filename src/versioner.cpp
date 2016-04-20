#undef NDEBUG
#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "android-base/stringprintf.h"
#include "clang/Tooling/Tooling.h"

#include "HeaderDatabase.h"
#include "LibraryDatabase.h"
#include "Utils.h"

using namespace clang::tooling;

static llvm::cl::OptionCategory VersionerCategory("versioner");

class HeaderCompilationDatabase : public CompilationDatabase {
  std::string cwd;
  std::vector<std::string> headers;
  std::vector<std::string> include_dirs;
  int api_level;

 public:
  HeaderCompilationDatabase(std::string cwd, std::vector<std::string> headers,
                            std::vector<std::string> include_dirs, int api_level)
      : cwd(std::move(cwd)),
        headers(std::move(headers)),
        include_dirs(std::move(include_dirs)),
        api_level(api_level) {
  }

  CompileCommand generateCompileCommand(const std::string& filename) const {
    std::vector<std::string> command = { "clang-tool", filename, "-nostdlibinc" };
    for (const auto& dir : include_dirs) {
      command.push_back("-isystem");
      command.push_back(dir);
    }
    command.push_back("-DANDROID");
    command.push_back(android::base::StringPrintf("-D__ANDROID_API__=%d", api_level));
    command.push_back("-D_FORTIFY_SOURCE=2");
    command.push_back("-D_GNU_SOURCE");
    command.push_back("-Wno-unknown-attributes");
    command.push_back("-target");
    command.push_back("arm-linux-androideabi");
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

static void compileHeaders(HeaderDatabase& database, const char* header_directory,
                            const char* dep_directory, int api_level) {
  std::string cwd = getWorkingDir();
  std::vector<std::string> headers = collectFiles(header_directory);

  std::vector<std::string> dependencies = { header_directory };
  if (dep_directory) {
    DIR* deps = opendir(dep_directory);
    if (!deps) {
      err(1, "failed to open dependency directory");
    }

    struct dirent* dent;
    while ((dent = readdir(deps))) {
      dependencies.push_back(std::string(dep_directory) + "/" + dent->d_name);
    }

    closedir(deps);
  }

  HeaderCompilationDatabase compilationDatabase(cwd, headers, dependencies, api_level);
  ClangTool tool(compilationDatabase, headers);

  std::vector<std::unique_ptr<clang::ASTUnit>> asts;
  tool.buildASTs(asts);
  for (const auto& ast : asts) {
    database.parseAST(ast.get(), api_level);
  }
}

const std::vector<int> default_apis = { 9, 12, 13, 14, 15, 16, 17, 17, 18, 19, 21, 23, 24 };

void usage() {
  printf("Usage: versioner [OPTION]... HEADER_PATH [DEPS_PATH]\n");
  printf("Compile and parse headers at HEADER_PATH, with DEPS_PATH on the include path");
  printf("\n");
  printf("\n");
  printf("Header compilation:\n");
  printf("  -a API_LEVEL\tbuild with the specified API level (can be repeated)\n");
  printf("    \t\tdefaults to %s\n", Join(default_apis, ", ").c_str());
  printf("  -f\t\tdump functions exposed in header dir\n");
  printf("  -v\t\tdump variables exposed in header dir\n");
  printf("  -m\t\tdump multiply-declared symbols\n");
  printf("\n");
  printf("Library inspection:\n");
  printf("  -l LIB_PATH\tinspect libraries at LIB_PATH\n");
  printf("  -d\t\tdump symbol availability in libraries\n");
  exit(1);
}

int main(int argc, char** argv) {
  HeaderDatabase headerDatabase;

  std::string cwd = getWorkingDir() + "/";
  std::vector<int> api_levels;
  bool default_args = true;
  bool dump_symbols = false;
  bool dump_multiply_declared = false;
  bool list_functions = false;
  bool list_variables = false;
  const char* library_dir = nullptr;

  int c;
  while ((c = getopt(argc, argv, "a:fvml:d")) != -1) {
    default_args = false;
    switch (c) {
      case 'a': {
        char *end;
        int api_level = strtol(optarg, &end, 10);
        if (end == optarg || strlen(end) > 0) {
          usage();
        }
        api_levels.push_back(api_level);
        break;
      }
      case 'f':
        list_functions = true;
        break;
      case 'v':
        list_variables = true;
        break;
      case 'm':
        dump_multiply_declared = true;
        break;
      case 'l': {
        if (library_dir) {
          usage();
        }

        library_dir = optarg;

        struct stat st;
        if (stat(library_dir, &st) != 0) {
          err(1, "failed to stat library dir");
        }
        if (!S_ISDIR(st.st_mode)) {
          errx(1, "%s is not a directory", optarg);
        }
        break;
      }
      case 'd':
        dump_symbols = true;
        break;
      default:
        usage();
        break;
    }
  }

  if (default_args) {
    dump_symbols = true;
    dump_multiply_declared = true;
  }

  if (argc - optind > 2 || optind >= argc) {
    usage();
  }

  if (api_levels.empty()) {
    api_levels = std::move(default_apis);
  }

  const char* dependencies = (argc - optind == 2) ? argv[optind + 1] : nullptr;
  for (int api_level : api_levels) {
    compileHeaders(headerDatabase, argv[optind], dependencies, api_level);
  }

  std::map<std::string, std::set<int>> symbol_database;
  for (int api_level : api_levels) {
    std::unordered_set<std::string> symbols;

    std::string api_dir = android::base::StringPrintf("%s/android-%d/", library_dir, api_level);
    std::vector<std::string> libraries = collectFiles(api_dir.c_str());
    for (const std::string& library : libraries) {
      std::unordered_set<std::string> lib_symbols = getSymbols(library);
      for (const std::string& symbol_name : lib_symbols) {
        symbol_database[symbol_name].insert(api_level);
      }
    }
  }

  if (dump_symbols) {
    printf("\nSymbols:\n");
    for (auto pair : symbol_database) {
      std::string message = pair.first + ": " +  Join(api_levels, ", ");
      printf("    %s\n", message.c_str());
    }
  }

  if (list_functions || list_variables) {
    std::set<std::string> functions;
    std::set<std::string> variables;
    for (const auto& pair : headerDatabase.symbols) {
      switch (pair.second.type()) {
        case SymbolType::function:
          functions.insert(pair.first);
          break;

        case SymbolType::variable:
          variables.insert(pair.first);
          break;

        case SymbolType::inconsistent:
          fprintf(stderr, "ERROR: inconsistent symbol type for %s", pair.first.c_str());
          pair.second.dump(cwd);
          exit(1);
      }
    }

    if (list_functions) {
      printf("\nFunctions:\n");
      for (const std::string& function : functions) {
        headerDatabase.symbols[function].dump(cwd);
      }
    }

    if (list_variables) {
      printf("\nVariables:\n");
      for (const std::string& variable : variables) {
        headerDatabase.symbols[variable].dump(cwd);
      }
    }
  }

  if (dump_multiply_declared) {
    std::vector<const Symbol*> multiply_declared;
    for (const auto& pair : headerDatabase.symbols) {
      for (int api_level : api_levels) {
        if (pair.second.getDeclarationType(api_level) == SymbolDeclarationType::multiply_declared) {
          multiply_declared.push_back(&pair.second);
          break;
        }
      }
    }

    printf("\n");

    if (multiply_declared.size() > 0) {
      printf("Multiply declared symbols:\n");
      for (const Symbol* symbol : multiply_declared) {
        symbol->dump(cwd);
      }
    } else {
      printf("No multiply declared symbols.\n");
    }
  }

  return 0;
}
