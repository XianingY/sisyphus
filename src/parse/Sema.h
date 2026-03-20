#ifndef SEMA_H
#define SEMA_H

#include "ASTNode.h"
#include "CompileError.h"
#include "Type.h"
#include "TypeContext.h"
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace sys {

// We don't need to do type inference, hence no memory management needed
class Sema {
  TypeContext &ctx;
  // The current function we're in. Mainly used for deducing return type.
  Type *currentFunc;

  using SymbolTable = std::map<std::string, Type*>;
  using MutabilityTable = std::map<std::string, bool>;
  SymbolTable symbols;
  MutabilityTable mutableSymbols;
  std::vector<std::unordered_set<std::string>> scopeDecls;

  class SemanticScope {
    Sema &sema;
    SymbolTable symbols;
    MutabilityTable mutableSymbols;
  public:
    SemanticScope(Sema &sema): sema(sema), symbols(sema.symbols), mutableSymbols(sema.mutableSymbols) {
      sema.scopeDecls.emplace_back();
    }
    ~SemanticScope() {
      sema.symbols = symbols;
      sema.mutableSymbols = mutableSymbols;
      sema.scopeDecls.pop_back();
    }
  };

  PointerType *decay(ArrayType *arrTy);
  ArrayType *raise(PointerType *ptr);
  [[noreturn]] void fail(const std::string &msg);
  void declareSymbol(const std::string &name, Type *ty, bool isMutable);
  bool isMutableSymbol(const std::string &name) const;

  Type *infer(ASTNode *node);
public:
  // This modifies `node` inplace.
  Sema(ASTNode *node, TypeContext &ctx);
};

}

#endif
