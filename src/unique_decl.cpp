#undef NDEBUG
#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <fts.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "android-base/stringprintf.h"
#include "clang/Tooling/Tooling.h"

#include "SymbolDatabase.h"

using namespace clang::tooling;

static const std::string& get_working_dir() {
  static std::string cwd;
  if (cwd.length() == 0) {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
      err(1, "getcwd failed");
    }
    cwd = buf;
  }
  return cwd;
}

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

static std::vector<std::string> collect_files(const char* directory) {
  std::vector<std::string> files;

  char* dir_argv[2] = { const_cast<char*>(directory), nullptr };
  FTS* fts = fts_open(dir_argv, FTS_LOGICAL | FTS_NOCHDIR, nullptr);

  if (!fts) {
    err(1, "failed to open directory '%s'", directory);
  }

  FTSENT* ent;
  while ((ent = fts_read(fts))) {
    if (ent->fts_info & (FTS_D | FTS_DP)) {
      continue;
    }

    files.push_back(ent->fts_path);
  }

  fts_close(fts);
  return files;
}

static void compile_headers(SymbolDatabase& database, const char* header_directory,
                            const char* dep_directory, int api_level) {
  std::string cwd = get_working_dir();
  std::vector<std::string> headers = collect_files(header_directory);

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
    database.parseAST(ast.get());
  }
}

void usage() {
  printf("usage: unique_decl [-d/-m/-f/-v] <header directory> [<header dependency directory>]\n");
  exit(1);
}

int main(int argc, char** argv) {
  SymbolDatabase symbolDatabase;

  int api_level = 10000;
  bool default_args = true;
  bool dump_symbols = false;
  bool dump_multiply_defined = false;
  bool list_functions = false;
  bool list_variables = false;

  int c;
  while ((c = getopt(argc, argv, "a:dmfv")) != -1) {
    default_args = false;
    switch (c) {
      case 'a': {
        char *end;
        api_level = strtol(optarg, &end, 10);
        if (end == optarg || strlen(end) > 0) {
          usage();
        }
        break;
      }
      case 'd':
        dump_symbols = true;
        break;
      case 'm':
        dump_multiply_defined = true;
        break;
      case 'f':
        list_functions = true;
        break;
      case 'v':
        list_variables = true;
        break;
      default:
        usage();
        break;
    }
  }

  if (default_args) {
    dump_symbols = true;
    dump_multiply_defined = true;
  }

  if (argc - optind > 2 || optind >= argc) {
    usage();
  }

  const char* dependencies = (argc - optind == 2) ? argv[optind + 1] : nullptr;
  compile_headers(symbolDatabase, argv[optind], dependencies, api_level);

  if (dump_symbols || list_functions || list_variables) {
    std::set<std::string> functions;
    std::set<std::string> variables;
    for (const auto& pair : symbolDatabase.symbols) {
      switch (pair.second.type()) {
        case SymbolType::function:
          functions.insert(pair.first);
          break;

        case SymbolType::variable:
          variables.insert(pair.first);
          break;

        case SymbolType::inconsistent:
          fprintf(stderr, "ERROR: inconsistent symbol type for %s", pair.first.c_str());
          exit(1);
      }
    }

    if (dump_symbols) {
      printf("Functions:\n");
      for (const std::string& function : functions) {
        symbolDatabase.symbols[function].dump(std::cout);
      }

      printf("\nVariables:\n");
      for (const std::string& variable : variables) {
        symbolDatabase.symbols[variable].dump(std::cout);
      }
      if (dump_multiply_defined) {
        printf("\n");
      }
    } else {
      if (list_functions) {
        for (const std::string& function : functions) {
          printf("%s\n", function.c_str());
        }
      }
      if (list_variables) {
        for (const std::string& variable : variables) {
          printf("%s\n", variable.c_str());
        }
      }
    }
  }

  if (dump_multiply_defined) {
    std::vector<const Symbol*> multiply_defined;
    for (const auto& pair : symbolDatabase.symbols) {
      if (pair.second.locations.size() > 1) {
        multiply_defined.push_back(&pair.second);
      }
    }

    if (multiply_defined.size() > 0) {
      printf("Multiply defined symbols:\n");
      for (const Symbol* symbol : multiply_defined) {
        symbol->dump(std::cout);
      }
    } else {
      printf("No multiply defined symbols.\n");
    }
  }

  return 0;
}
