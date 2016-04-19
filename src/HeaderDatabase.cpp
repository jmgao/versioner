#include "HeaderDatabase.h"

#include <iostream>
#include <set>
#include <string>
#include <unordered_map>

#include "clang/AST/AST.h"
#include "clang/AST/Attr.h"
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

  std::string getDeclName(NamedDecl* decl) {
    if (VarDecl* var_decl = dyn_cast<VarDecl>(decl)) {
      if (!var_decl->isFileVarDecl()) {
        return "<local var>";
      }
    }

    if (mangler->shouldMangleDeclName(decl)) {
      std::string mangled;
      llvm::raw_string_ostream ss(mangled);
      mangler->mangleName(decl, ss);
      return mangled;
    }

    auto identifier = decl->getIdentifier();
    if (!identifier) {
      return "<error>";
    }
    return identifier->getName();
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

    std::string symbol_name = getDeclName(named_decl);
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

        case VarDecl::Definition:
          is_definition = true;
          break;

        case VarDecl::TentativeDefinition:
          // Forbid tentative definitions in headers.
          fprintf(stderr, "ERROR: symbol '%s' is a tentative definition\n", symbol_name.c_str());
          decl->dump();
          abort();
      }
    } else {
      return true;
    }

    // Look for availability annotations.
    SymbolAvailability availability;
    for (const AvailabilityAttr* attr : decl->specific_attrs<AvailabilityAttr>()) {
      if (attr->getPlatform()->getName() != "android") {
        fprintf(stderr, "skipping non-android platform %s\n",
                attr->getPlatform()->getName().str().c_str());
        continue;
      }
      if (attr->getIntroduced().getMajor() != 0) {
        availability.introduced = attr->getIntroduced().getMajor();
      }
      if (attr->getDeprecated().getMajor() != 0) {
        availability.deprecated = attr->getDeprecated().getMajor();
      }
      if (attr->getObsoleted().getMajor()) {
        availability.obsoleted = attr->getObsoleted().getMajor();
      }
    }

    // Find or insert an entry for the symbol.
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
      .availability = availability,
    };

    // It's fine if the location is already there, we'll get an iterator to the existing element.
    auto location_it = symbol_locations.begin();
    bool inserted = false;
    std::tie(location_it, inserted) = symbol_locations.insert(location);

    // If we didn't insert, check to see if the availability attributes are identical.
    if (!inserted) {
      if (location_it->availability != availability) {
        fprintf(stderr, "ERROR: availability attribute mismatch\n");
        decl->dump();
        abort();
      }
    }

    location_it->addAPI(api_level);

    return true;
  }
};

void HeaderDatabase::parseAST(ASTUnit* ast, int api_level) {
  ASTContext& ctx = ast->getASTContext();
  Visitor visitor(*this, ctx, api_level);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}
