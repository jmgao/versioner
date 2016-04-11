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

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
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

enum class Availability {
  unknown,
  available,
  unavailable,
};

typedef int APILevel;

static llvm::cl::OptionCategory VersionerCategory("versioner");

enum class SymbolType {
  function,
  variable,
  inconsistent,
};

struct SymbolLocation {
  SymbolType type;
  std::string filename;
  unsigned lineNumber;

  auto tie() const {
    return std::tie(filename, lineNumber);
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
  std::unordered_map<APILevel, Availability> availability;
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
    out << "\t" << name << " declared in " << locations.size() << " locations:\n";
    for (auto location : locations) {
      const char* var_type = (location.type == SymbolType::function) ? "function" : "variable";
      out << "\t\t" << var_type << " @ " << location.filename << ":" << location.lineNumber << "\n";
    }
  }
};

struct SymbolDatabase {
  std::unordered_map<std::string, Symbol> symbols;

  void registerSymbol(const std::string& symbolName, SymbolType symbolType, std::string filename,
                      unsigned lineNumber) {
    auto it = symbols.find(symbolName);

    if (it == symbols.end()) {
      Symbol symbol = {.name = symbolName };
      bool inserted;
      std::tie(it, inserted) = symbols.insert(decltype(symbols)::value_type(symbolName, symbol));
      assert(inserted);
    }

    SymbolLocation location = {
      .type = symbolType, .filename = std::move(filename), .lineNumber = lineNumber
    };
    it->second.locations.insert(location);
  }

  void dump(std::ostream& out = std::cout) const {
    out << "SymbolDatabase contains " << symbols.size() << " symbols:\n";
    std::vector<const Symbol*> multiply_defined;
    for (const auto& pair : symbols) {
      pair.second.dump(out);
      if (pair.second.locations.size() > 1) {
        multiply_defined.push_back(&pair.second);
      }
    }

    if (multiply_defined.size() > 0) {
      out << "\nMultiply defined symbols:\n";
      for (const Symbol* symbol : multiply_defined) {
        symbol->dump(out);
      }
    }
  }
};

class Visitor : public clang::RecursiveASTVisitor<Visitor> {
  SymbolDatabase& database;

 public:
  Visitor(SymbolDatabase& database) : database(database) {
  }

  bool VisitDecl(Decl* decl) {
    if (decl->getParentFunctionOrMethod()) {
      return true;
    }

    ASTContext& ctx = decl->getASTContext();
    SourceManager& src_manager = ctx.getSourceManager();

    auto namedDecl = dyn_cast<NamedDecl>(decl);
    if (!namedDecl) {
      return true;
    }

    SymbolType symbolType;
    FunctionDecl* function_decl = dyn_cast<FunctionDecl>(decl);
    VarDecl* var_decl = dyn_cast<VarDecl>(decl);
    if (function_decl) {
      symbolType = SymbolType::function;
    } else if (var_decl) {
      symbolType = SymbolType::variable;
      if (!var_decl->hasExternalStorage()) {
        return true;
      }
    } else {
      return true;
    }

    auto location = src_manager.getPresumedLoc(decl->getLocation());
    StringRef filename = location.getFilename();
    unsigned lineNumber = location.getLine();

    // TODO: Mangle the symbol name.
    database.registerSymbol(namedDecl->getDeclName().getAsString(), symbolType, std::move(filename),
                            lineNumber);
    return true;
  }
};

class HeaderCompilationDatabase : public CompilationDatabase {
  std::string cwd;
  std::vector<std::string> headers;
  std::vector<std::string> include_dirs;

 public:
  HeaderCompilationDatabase(std::string cwd, std::vector<std::string> headers,
                            std::vector<std::string> include_dirs)
      : cwd(std::move(cwd)), headers(std::move(headers)), include_dirs(std::move(include_dirs)){
  }

  CompileCommand generateCompileCommand(const std::string& filename) const {
    std::vector<std::string> command = { "clang-tool", filename, "-nostdlibinc" };
    for (const auto& dir : include_dirs) {
      command.push_back("-isystem");
      command.push_back(dir);
    }
    command.push_back("-DANDROID");
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
                            const char* dep_directory) {
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

  HeaderCompilationDatabase compilationDatabase(cwd, headers, dependencies);
  ClangTool tool(compilationDatabase, headers);
  Visitor visitor(database);

  std::vector<std::unique_ptr<ASTUnit>> asts;
  tool.buildASTs(asts);
  for (const auto& ast : asts) {
    visitor.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
  }
}

int main(int argc, const char** argv) {
  SymbolDatabase symbolDatabase;

  if (argc != 3) {
    printf("usage: unique_decl [header directory] [header dependency directory]\n");
    return 1;
  }

  compile_headers(symbolDatabase, argv[1], argv[2]);
  symbolDatabase.dump();
  return 0;
}
