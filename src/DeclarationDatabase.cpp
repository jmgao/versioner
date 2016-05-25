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

#include "DeclarationDatabase.h"

#include <iostream>
#include <map>
#include <set>
#include <string>

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

 public:
  Visitor(HeaderDatabase& database, ASTContext& ctx) : database(database) {
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

    DeclarationType declaration_type;
    FunctionDecl* function_decl = dyn_cast<FunctionDecl>(decl);
    VarDecl* var_decl = dyn_cast<VarDecl>(decl);

    std::string declaration_name = getDeclName(named_decl);
    bool is_extern = named_decl->getFormalLinkage() == ExternalLinkage;
    bool is_definition = false;

    if (function_decl) {
      declaration_type = DeclarationType::function;
      is_definition = function_decl->isThisDeclarationADefinition();
    } else if (var_decl) {
      if (!var_decl->isFileVarDecl()) {
        return true;
      }

      declaration_type = DeclarationType::variable;
      switch (var_decl->isThisDeclarationADefinition()) {
        case VarDecl::DeclarationOnly:
          is_definition = false;
          break;

        case VarDecl::Definition:
          is_definition = true;
          break;

        case VarDecl::TentativeDefinition:
          // Forbid tentative definitions in headers.
          fprintf(stderr, "ERROR: declaration '%s' is a tentative definition\n",
                  declaration_name.c_str());
          decl->dump();
          abort();
      }
    } else {
      return true;
    }

    if (decl->hasAttr<UnavailableAttr>()) {
      // Skip declarations that exist only for compile-time diagnostics.
      return true;
    }

    // Look for availability annotations.
    DeclarationAvailability availability;
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
      if (attr->getObsoleted().getMajor() != 0) {
        availability.obsoleted = attr->getObsoleted().getMajor();
      }
    }

    // Find or insert an entry for the declaration.
    auto declaration_it = database.declarations.find(declaration_name);
    if (declaration_it == database.declarations.end()) {
      Declaration declaration = {.name = declaration_name };
      bool inserted;
      std::tie(declaration_it, inserted) =
        database.declarations.insert({ declaration_name, declaration });
    }

    auto& declaration_locations = declaration_it->second.locations;
    auto presumed_loc = src_manager.getPresumedLoc(decl->getLocation());
    DeclarationLocation location = {
      .filename = presumed_loc.getFilename(),
      .line_number = presumed_loc.getLine(),
      .column = presumed_loc.getColumn(),
      .type = declaration_type,
      .is_extern = is_extern,
      .is_definition = is_definition,
      .availability = availability,
    };

    // It's fine if the location is already there, we'll get an iterator to the existing element.
    auto location_it = declaration_locations.begin();
    bool inserted = false;
    std::tie(location_it, inserted) = declaration_locations.insert(location);

    // If we didn't insert, check to see if the availability attributes are identical.
    if (!inserted) {
      if (location_it->availability != availability) {
        fprintf(stderr, "ERROR: availability attribute mismatch\n");
        decl->dump();
        abort();
      }
    }

    return true;
  }
};

void HeaderDatabase::parseAST(ASTUnit* ast) {
  ASTContext& ctx = ast->getASTContext();
  Visitor visitor(*this, ctx);
  visitor.TraverseDecl(ctx.getTranslationUnitDecl());
}
