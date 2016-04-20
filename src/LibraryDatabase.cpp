#include "LibraryDatabase.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <unordered_set>

#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"

using namespace llvm;
using namespace llvm::object;

std::unordered_set<std::string> getSymbols(const std::string& filename) {
  std::unordered_set<std::string> result;
  auto binary = createBinary(filename);
  if (std::error_code ec = binary.getError()) {
    fprintf(stderr, "ERROR: failed to open library at %s: %s\n", filename.c_str(),
            ec.message().c_str());
    abort();
  }

  ELFObjectFileBase* elf = dyn_cast_or_null<ELFObjectFileBase>(binary.get().getBinary());
  if (!elf) {
    fprintf(stderr, "ERROR: failed to parse %s as ELF\n", filename.c_str());
    abort();
  }

  for (const ELFSymbolRef symbol : elf->getDynamicSymbolIterators()) {
    ErrorOr<StringRef> symbol_name = symbol.getName();

    if (std::error_code ec = binary.getError()) {
      fprintf(stderr, "ERROR: failed to get symbol name for symbol in %s: %s", filename.c_str(),
              ec.message().c_str());
      abort();
    }

    result.insert(symbol_name.get().str());
  }

  return result;
}
