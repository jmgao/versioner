#include "HeaderDatabase.h"

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

class Visitor : public RecursiveASTVisitor<Visitor> {
  HeaderDatabase& database;
  std::unique_ptr<MangleContext> mangler;
  int api_level;

 public:
  Visitor(HeaderDatabase& database, ASTContext& ctx, int api_level)
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

    // Find or insert an entry for the symbol.
    std::string symbol_name = mangleDecl(named_decl);
    auto symbol_it = database.symbols.find(symbol_name);
    if (symbol_it == database.symbols.end()) {
      Symbol symbol = {.name = symbol_name };
      bool inserted;
      std::tie(symbol_it, inserted) = database.symbols.insert({ symbol_name, symbol });
    }

    auto& symbol_locations = symbol_it->second.locations;
    auto presumed_loc = src_manager.getPresumedLoc(decl->getLocation());
    SymbolLocation location = {
      .filename = presumed_loc.getFilename(),
      .line_number = presumed_loc.getLine(),
      .column = presumed_loc.getColumn(),
      .type = symbol_type,
      .is_extern = is_extern,
      .is_definition = is_definition,
    };

    // It's fine if the location is already there, we'll get an iterator to the existing element.
    auto location_it = symbol_locations.insert(symbol_locations.begin(), location);
    location_it->addAPI(api_level);

    return true;
  }
};

void HeaderDatabase::parseAST(ASTUnit* ast, int api_level) {
  ASTContext& ctx = ast->getASTContext();
  Visitor visitor(*this, ctx, api_level);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}
