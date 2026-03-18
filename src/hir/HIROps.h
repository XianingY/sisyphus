#ifndef HIR_OPS_H
#define HIR_OPS_H

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "../parse/ASTNode.h"
#include "../parse/Type.h"

namespace sys::hir {

enum class TypeKind {
  Unknown,
  Int,
  Float,
  Void,
  Pointer,
  Array,
  Function,
};

enum Trait : uint32_t {
  TraitNone = 0,
  Pure = 1u << 0,
  MemoryEffect = 1u << 1,
  BranchLike = 1u << 2,
  LoopLike = 1u << 3,
};

inline uint32_t operator|(Trait lhs, Trait rhs) {
  return (uint32_t) lhs | (uint32_t) rhs;
}

inline bool hasTrait(uint32_t traits, Trait trait) {
  return (traits & (uint32_t) trait) != 0;
}

enum class OpKind {
  Unknown,
  Module,
  Func,
  Block,
  If,
  While,
  For,
  Call,
  Load,
  Store,
  Arith,
  Cmp,
  Return,
  VarDecl,
  Break,
  Continue,
  ConstInt,
  ConstFloat,
};

struct Op {
  OpKind kind;
  TypeKind type;
  uint32_t traits;
  ASTNode *origin;
  std::string symbol;

  bool hasIntValue;
  long long intValue;
  bool hasFloatValue;
  double floatValue;

  std::vector<int> arrayDims;
  std::vector<std::unique_ptr<Op>> children;

  explicit Op(OpKind kind, ASTNode *origin = nullptr);
  Op *append(std::unique_ptr<Op> child);
};

struct Module {
  ASTNode *originAst = nullptr;
  std::unique_ptr<Op> root;
};

TypeKind mapType(Type *type);
uint32_t defaultTraits(OpKind kind);
const char *kindName(OpKind kind);
void dump(const Module &module, std::ostream &os);

}  // namespace sys::hir

#endif
