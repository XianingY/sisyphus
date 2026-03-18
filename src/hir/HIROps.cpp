#include "HIROps.h"

#include <iomanip>

#include "../utils/DynamicCast.h"

namespace sys::hir {

namespace {

void dumpOp(const Op *op, std::ostream &os, int depth) {
  if (!op)
    return;

  for (int i = 0; i < depth; i++)
    os << "  ";
  os << kindName(op->kind);
  if (!op->symbol.empty())
    os << " \"" << op->symbol << "\"";
  if (op->hasIntValue)
    os << " value=" << op->intValue;
  if (op->hasFloatValue)
    os << " value=" << std::setprecision(9) << op->floatValue;
  os << "\n";

  for (const auto &child : op->children)
    dumpOp(child.get(), os, depth + 1);
}

}  // namespace

Op::Op(OpKind kind, ASTNode *origin):
  kind(kind),
  type(TypeKind::Unknown),
  traits(defaultTraits(kind)),
  origin(origin),
  hasIntValue(false),
  intValue(0),
  hasFloatValue(false),
  floatValue(0.0) {}

Op *Op::append(std::unique_ptr<Op> child) {
  if (!child)
    return nullptr;
  children.push_back(std::move(child));
  return children.back().get();
}

TypeKind mapType(Type *type) {
  if (!type)
    return TypeKind::Unknown;
  if (isa<IntType>(type))
    return TypeKind::Int;
  if (isa<FloatType>(type))
    return TypeKind::Float;
  if (isa<VoidType>(type))
    return TypeKind::Void;
  if (isa<PointerType>(type))
    return TypeKind::Pointer;
  if (isa<ArrayType>(type))
    return TypeKind::Array;
  if (isa<FunctionType>(type))
    return TypeKind::Function;
  return TypeKind::Unknown;
}

uint32_t defaultTraits(OpKind kind) {
  switch (kind) {
  case OpKind::Module:
  case OpKind::Func:
  case OpKind::Block:
  case OpKind::VarDecl:
  case OpKind::ConstInt:
  case OpKind::ConstFloat:
    return TraitNone;
  case OpKind::If:
    return BranchLike;
  case OpKind::While:
  case OpKind::For:
    return BranchLike | LoopLike;
  case OpKind::Call:
    return MemoryEffect;
  case OpKind::Load:
    return MemoryEffect;
  case OpKind::Store:
    return MemoryEffect;
  case OpKind::Arith:
  case OpKind::Cmp:
    return Pure;
  case OpKind::Return:
  case OpKind::Break:
  case OpKind::Continue:
    return BranchLike;
  case OpKind::Unknown:
    return TraitNone;
  }
  return TraitNone;
}

const char *kindName(OpKind kind) {
  switch (kind) {
  case OpKind::Unknown:
    return "unknown";
  case OpKind::Module:
    return "module";
  case OpKind::Func:
    return "func";
  case OpKind::Block:
    return "block";
  case OpKind::If:
    return "if";
  case OpKind::While:
    return "while";
  case OpKind::For:
    return "for";
  case OpKind::Call:
    return "call";
  case OpKind::Load:
    return "load";
  case OpKind::Store:
    return "store";
  case OpKind::Arith:
    return "arith";
  case OpKind::Cmp:
    return "cmp";
  case OpKind::Return:
    return "return";
  case OpKind::VarDecl:
    return "vardecl";
  case OpKind::Break:
    return "break";
  case OpKind::Continue:
    return "continue";
  case OpKind::ConstInt:
    return "const.int";
  case OpKind::ConstFloat:
    return "const.float";
  }
  return "unknown";
}

void dump(const Module &module, std::ostream &os) {
  if (!module.root) {
    os << "<null hir module>\n";
    return;
  }
  dumpOp(module.root.get(), os, 0);
}

}  // namespace sys::hir
