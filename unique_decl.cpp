#undef NDEBUG
#include <assert.h>

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

    SymbolLocation location = {.filename = std::move(filename), .lineNumber = lineNumber };
    it->second.locations.insert(location);
  }

  void dump(std::ostream& out = std::cout) const {
    out << "SymbolDatabase contains " << symbols.size() << " symbols:\n";
    for (const auto& pair : symbols) {
      pair.second.dump(out);
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
    if (isa<FunctionDecl>(decl)) {
      symbolType = SymbolType::function;
    } else if (isa<VarDecl>(decl)) {
      symbolType = SymbolType::variable;
    } else {
      return true;
    }

    auto location = decl->getLocation();
    StringRef filename = src_manager.getFilename(location);
    unsigned lineNumber = src_manager.getSpellingLineNumber(location);

    // TODO: Mangle the symbol name.
    database.registerSymbol(namedDecl->getDeclName().getAsString(), symbolType, std::move(filename),
                            lineNumber);
    return true;
  }
};

int main(int argc, const char** argv) {
  CommonOptionsParser OptionsParser(argc, argv, VersionerCategory);
  ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

  std::vector<std::unique_ptr<ASTUnit>> asts;
  Tool.buildASTs(asts);

  SymbolDatabase symbolDatabase;
  Visitor visitor(symbolDatabase);
  for (const auto& ast : asts) {
    visitor.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
  }
  symbolDatabase.dump();
  return 0;
}
