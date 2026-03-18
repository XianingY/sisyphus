#include "HIRBuilder.h"

#include "../utils/DynamicCast.h"

namespace sys::hir {

namespace {

const char *binarySymbol(int kind) {
  switch (kind) {
  case BinaryNode::Add:
    return "+";
  case BinaryNode::Sub:
    return "-";
  case BinaryNode::Mul:
    return "*";
  case BinaryNode::Div:
    return "/";
  case BinaryNode::Mod:
    return "%";
  case BinaryNode::And:
    return "&&";
  case BinaryNode::Or:
    return "||";
  case BinaryNode::Eq:
    return "==";
  case BinaryNode::Ne:
    return "!=";
  case BinaryNode::Le:
    return "<=";
  case BinaryNode::Lt:
    return "<";
  }
  return "?";
}

const char *unarySymbol(int kind) {
  switch (kind) {
  case UnaryNode::Not:
    return "!";
  case UnaryNode::Minus:
    return "-";
  case UnaryNode::Float2Int:
    return "f2i";
  case UnaryNode::Int2Float:
    return "i2f";
  }
  return "?";
}

bool isCmp(int kind) {
  return kind == BinaryNode::Eq || kind == BinaryNode::Ne ||
    kind == BinaryNode::Le || kind == BinaryNode::Lt;
}

std::string assignTargetName(ASTNode *lhs) {
  if (!lhs)
    return "";
  if (auto ref = dyn_cast<VarRefNode>(lhs))
    return ref->name;
  if (auto arr = dyn_cast<ArrayAccessNode>(lhs))
    return arr->array;
  return "";
}

}  // namespace

Module Builder::build(ASTNode *node) {
  Module module;
  module.originAst = node;
  module.root = std::make_unique<Op>(OpKind::Module, node);
  if (!node)
    return module;

  if (auto topBlock = dyn_cast<BlockNode>(node)) {
    for (auto *child : topBlock->nodes)
      module.root->append(buildNode(child));
    return module;
  }

  if (auto topBlock = dyn_cast<TransparentBlockNode>(node)) {
    for (auto *decl : topBlock->nodes)
      module.root->append(buildNode(decl));
    return module;
  }

  module.root->append(buildNode(node));
  return module;
}

std::unique_ptr<Op> Builder::buildBlockLike(ASTNode *node) {
  auto block = std::make_unique<Op>(OpKind::Block, node);
  block->type = TypeKind::Unknown;
  if (!node)
    return block;

  if (auto *b = dyn_cast<BlockNode>(node)) {
    for (auto *child : b->nodes)
      block->append(buildNode(child));
    return block;
  }
  if (auto *b = dyn_cast<TransparentBlockNode>(node)) {
    for (auto *decl : b->nodes)
      block->append(buildNode(decl));
    return block;
  }

  block->append(buildNode(node));
  return block;
}

std::unique_ptr<Op> Builder::buildNode(ASTNode *node) {
  if (!node)
    return nullptr;

  if (auto *n = dyn_cast<IntNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::ConstInt, node);
    op->type = TypeKind::Int;
    op->hasIntValue = true;
    op->intValue = n->value;
    return op;
  }
  if (auto *n = dyn_cast<FloatNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::ConstFloat, node);
    op->type = TypeKind::Float;
    op->hasFloatValue = true;
    op->floatValue = n->value;
    return op;
  }
  if (auto *n = dyn_cast<VarDeclNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::VarDecl, node);
    op->type = mapType(node->type);
    op->symbol = n->name;
    if (auto *arrTy = dyn_cast<ArrayType>(node->type))
      op->arrayDims = arrTy->dims;
    op->append(buildNode(n->init));
    return op;
  }
  if (auto *n = dyn_cast<VarRefNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Load, node);
    op->symbol = n->name;
    op->type = mapType(node->type);
    return op;
  }
  if (auto *n = dyn_cast<ArrayAccessNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Load, node);
    op->symbol = n->array;
    op->type = mapType(node->type);
    for (auto *idx : n->indices)
      op->append(buildNode(idx));
    return op;
  }
  if (auto *n = dyn_cast<ArrayAssignNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Store, node);
    op->symbol = n->array;
    op->type = mapType(node->type);
    for (auto *idx : n->indices)
      op->append(buildNode(idx));
    op->append(buildNode(n->value));
    return op;
  }
  if (auto *n = dyn_cast<AssignNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Store, node);
    op->type = mapType(node->type);
    op->symbol = assignTargetName(n->l);
    if (auto *lhsArr = dyn_cast<ArrayAccessNode>(n->l)) {
      for (auto *idx : lhsArr->indices)
        op->append(buildNode(idx));
    }
    op->append(buildNode(n->r));
    return op;
  }
  if (auto *n = dyn_cast<UnaryNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Arith, node);
    op->type = mapType(node->type);
    op->symbol = unarySymbol(n->kind);
    op->append(buildNode(n->node));
    return op;
  }
  if (auto *n = dyn_cast<BinaryNode>(node)) {
    auto kind = isCmp(n->kind) ? OpKind::Cmp : OpKind::Arith;
    auto op = std::make_unique<Op>(kind, node);
    op->type = isCmp(n->kind) ? TypeKind::Int : mapType(node->type);
    op->symbol = binarySymbol(n->kind);
    op->append(buildNode(n->l));
    op->append(buildNode(n->r));
    return op;
  }
  if (auto *n = dyn_cast<CallNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Call, node);
    op->symbol = n->func;
    op->type = mapType(node->type);
    for (auto *arg : n->args)
      op->append(buildNode(arg));
    return op;
  }
  if (auto *n = dyn_cast<ReturnNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Return, node);
    op->symbol = n->func;
    op->type = mapType(node->type);
    op->append(buildNode(n->node));
    return op;
  }
  if (auto *n = dyn_cast<IfNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::If, node);
    op->type = TypeKind::Unknown;
    op->append(buildNode(n->cond));
    op->append(buildBlockLike(n->ifso));
    if (n->ifnot)
      op->append(buildBlockLike(n->ifnot));
    return op;
  }
  if (auto *n = dyn_cast<WhileNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::While, node);
    op->type = TypeKind::Unknown;
    op->append(buildNode(n->cond));
    op->append(buildBlockLike(n->body));
    return op;
  }
  if (auto *n = dyn_cast<FnDeclNode>(node)) {
    auto op = std::make_unique<Op>(OpKind::Func, node);
    op->symbol = n->name;
    if (auto *fnTy = dyn_cast<FunctionType>(node->type))
      op->type = mapType(fnTy->ret);
    else
      op->type = mapType(node->type);
    op->append(buildBlockLike(n->body));
    return op;
  }
  if (isa<ConstArrayNode>(node) || isa<LocalArrayNode>(node)) {
    // Keep array initializers in a legal placeholder form for HIR-stage
    // verification. The real semantics are preserved in CFG->legacy lowering.
    auto op = std::make_unique<Op>(OpKind::ConstInt, node);
    op->type = mapType(node->type);
    op->hasIntValue = true;
    op->intValue = 0;
    return op;
  }

  if (isa<BlockNode>(node) || isa<TransparentBlockNode>(node))
    return buildBlockLike(node);

  if (isa<BreakNode>(node))
    return std::make_unique<Op>(OpKind::Break, node);
  if (isa<ContinueNode>(node))
    return std::make_unique<Op>(OpKind::Continue, node);
  if (isa<EmptyNode>(node))
    return std::make_unique<Op>(OpKind::Block, node);

  auto unknown = std::make_unique<Op>(OpKind::Unknown, node);
  unknown->type = mapType(node->type);
  return unknown;
}

}  // namespace sys::hir
