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
                           SymbolType symbol_type, bool is_extern, bool is_definition,
                           PresumedLoc presumed_loc, int api_level) {
  auto it = database.symbols.find(symbol_name);

  if (it == database.symbols.end()) {
    Symbol symbol = {.name = symbol_name };
    bool inserted;
    std::tie(it, inserted) = database.symbols.insert({ symbol_name, symbol });
    assert(inserted);
  }

  auto& locations = it->second.locations;

  SymbolLocation location = {
    .filename = presumed_loc.getFilename(),
    .line_number = presumed_loc.getLine(),
    .column = presumed_loc.getColumn(),
    .type = symbol_type,
    .is_extern = is_extern,
    .is_definition = is_definition,
  };
  auto location_it = locations.insert(locations.begin(), location);
  location_it->addAPI(api_level);
}

class Visitor : public RecursiveASTVisitor<Visitor> {
  SymbolDatabase& database;
  std::unique_ptr<MangleContext> mangler;
  int api_level;

 public:
  Visitor(SymbolDatabase& database, ASTContext& ctx, int api_level)
      : database(database), api_level(api_level) {
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

    bool is_extern = named_decl->getFormalLinkage() == ExternalLinkage;
    bool is_definition = false;

    if (function_decl) {
      symbol_type = SymbolType::function;
      is_definition = function_decl->isThisDeclarationADefinition();
    } else if (var_decl) {
      if (!var_decl->isFileVarDecl()) {
        return true;
      }

      symbol_type = SymbolType::variable;
      switch (var_decl->isThisDeclarationADefinition()) {
        case VarDecl::DeclarationOnly:
          is_definition = false;
          break;

        case VarDecl::TentativeDefinition:
        // Assume that tenative definitions are always definitions.
        // If this isn't true, we can always hoist the actual definition out into its own header
        // to avoid a false positive.
        case VarDecl::Definition:
          is_definition = true;
          break;
      }
    } else {
      return true;
    }

    auto location = src_manager.getPresumedLoc(decl->getLocation());
    registerSymbol(database, mangleDecl(named_decl), symbol_type, is_extern, is_definition,
                   location, api_level);
    return true;
  }
};

void SymbolDatabase::parseAST(ASTUnit* ast, int api_level) {
  ASTContext& ctx = ast->getASTContext();
  Visitor visitor(*this, ctx, api_level);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}
