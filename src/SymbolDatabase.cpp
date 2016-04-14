#include "SymbolDatabase.h"

#include <iostream>
#include <set>
#include <string>
#include <unordered_map>

#include "clang/AST/AST.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTUnit.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

static void registerSymbol(SymbolDatabase& database, const std::string& symbol_name,
                           SymbolType symbol_type, std::string filename, unsigned line_number) {
  auto it = database.symbols.find(symbol_name);

  if (it == database.symbols.end()) {
    Symbol symbol = {.name = symbol_name };
    bool inserted;
    std::tie(it, inserted) =
      database.symbols.insert(decltype(database.symbols)::value_type(symbol_name, symbol));
    assert(inserted);
  }

  SymbolLocation location = {
    .type = symbol_type, .filename = std::move(filename), .line_number = line_number
  };
  it->second.locations.insert(location);
}

class Visitor : public RecursiveASTVisitor<Visitor> {
  SymbolDatabase& database;
  std::unique_ptr<MangleContext> mangler;

 public:
  Visitor(SymbolDatabase& database, ASTContext& ctx) : database(database) {
    mangler.reset(ItaniumMangleContext::create(ctx, ctx.getDiagnostics()));
  }

  std::string mangleDecl(NamedDecl* decl) {
    if (mangler->shouldMangleDeclName(decl)) {
      std::string mangled;
      llvm::raw_string_ostream ss(mangled);
      mangler->mangleName(decl, ss);
      return mangled;
    }

    return decl->getIdentifier()->getName();
  }

  bool VisitDecl(Decl* decl) {
    if (decl->getParentFunctionOrMethod()) {
      return true;
    }

    ASTContext& ctx = decl->getASTContext();
    SourceManager& src_manager = ctx.getSourceManager();

    auto named_decl = dyn_cast<NamedDecl>(decl);
    if (!named_decl) {
      return true;
    }

    SymbolType symbol_type;
    FunctionDecl* function_decl = dyn_cast<FunctionDecl>(decl);
    VarDecl* var_decl = dyn_cast<VarDecl>(decl);
    if (function_decl) {
      symbol_type = SymbolType::function;
    } else if (var_decl) {
      symbol_type = SymbolType::variable;
      if (!var_decl->hasExternalStorage()) {
        return true;
      }
    } else {
      return true;
    }

    auto location = src_manager.getPresumedLoc(decl->getLocation());
    StringRef filename = location.getFilename();
    unsigned line_number = location.getLine();

    registerSymbol(database, mangleDecl(named_decl), symbol_type, std::move(filename), line_number);
    return true;
  }
};

void SymbolDatabase::parseAST(ASTUnit* ast) {
  ASTContext& ctx = ast->getASTContext();
  Visitor visitor(*this, ctx);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}
