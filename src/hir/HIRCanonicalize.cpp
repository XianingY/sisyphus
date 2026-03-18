#include "HIRCanonicalize.h"

#include <cmath>
#include <utility>
#include <vector>

namespace sys::hir {

namespace {

bool isConstInt(const Op *op) {
  return op && op->kind == OpKind::ConstInt && op->hasIntValue;
}

bool isConstFloat(const Op *op) {
  return op && op->kind == OpKind::ConstFloat && op->hasFloatValue;
}

void rewriteToConstInt(Op *op, long long value) {
  op->kind = OpKind::ConstInt;
  op->type = TypeKind::Int;
  op->traits = defaultTraits(OpKind::ConstInt);
  op->hasIntValue = true;
  op->intValue = value;
  op->hasFloatValue = false;
  op->floatValue = 0.0;
  op->children.clear();
  op->symbol.clear();
}

void rewriteToConstFloat(Op *op, double value) {
  op->kind = OpKind::ConstFloat;
  op->type = TypeKind::Float;
  op->traits = defaultTraits(OpKind::ConstFloat);
  op->hasFloatValue = true;
  op->floatValue = value;
  op->hasIntValue = false;
  op->intValue = 0;
  op->children.clear();
  op->symbol.clear();
}

bool takeTruthyBranch(Op *cond) {
  if (isConstInt(cond))
    return cond->intValue != 0;
  if (isConstFloat(cond))
    return cond->floatValue != 0.0;
  return true;
}

}  // namespace

CanonStats Canonicalizer::run(Module &module) {
  CanonStats stats;
  if (!module.root)
    return stats;

  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<Op*> stack = { module.root.get() };
    while (!stack.empty()) {
      Op *op = stack.back();
      stack.pop_back();
      changed = foldConstExpr(op, stats) || changed;
      changed = simplifyStructuredControl(op, stats) || changed;
      for (auto it = op->children.rbegin(); it != op->children.rend(); ++it)
        if (it->get())
          stack.push_back(it->get());
    }
  }
  return stats;
}

bool Canonicalizer::foldConstExpr(Op *op, CanonStats &stats) {
  if (!op)
    return false;

  if (op->kind != OpKind::Arith && op->kind != OpKind::Cmp)
    return false;
  if (op->children.size() < 2)
    return false;

  Op *lhs = op->children[0].get();
  Op *rhs = op->children[1].get();
  if (!lhs || !rhs)
    return false;

  if (isConstInt(lhs) && isConstInt(rhs)) {
    long long l = lhs->intValue;
    long long r = rhs->intValue;
    bool changed = true;
    if (op->kind == OpKind::Cmp) {
      if (op->symbol == "==")
        rewriteToConstInt(op, l == r);
      else if (op->symbol == "!=")
        rewriteToConstInt(op, l != r);
      else if (op->symbol == "<")
        rewriteToConstInt(op, l < r);
      else if (op->symbol == "<=")
        rewriteToConstInt(op, l <= r);
      else
        changed = false;
    } else {
      if (op->symbol == "+")
        rewriteToConstInt(op, l + r);
      else if (op->symbol == "-")
        rewriteToConstInt(op, l - r);
      else if (op->symbol == "*")
        rewriteToConstInt(op, l * r);
      else if (op->symbol == "/" && r != 0)
        rewriteToConstInt(op, l / r);
      else if (op->symbol == "%" && r != 0)
        rewriteToConstInt(op, l % r);
      else if (op->symbol == "&&")
        rewriteToConstInt(op, (l != 0) && (r != 0));
      else if (op->symbol == "||")
        rewriteToConstInt(op, (l != 0) || (r != 0));
      else
        changed = false;
    }
    if (changed)
      stats.constFolded++;
    return changed;
  }

  if (isConstFloat(lhs) && isConstFloat(rhs)) {
    double l = lhs->floatValue;
    double r = rhs->floatValue;
    bool changed = true;
    if (op->kind == OpKind::Cmp) {
      if (op->symbol == "==")
        rewriteToConstInt(op, l == r);
      else if (op->symbol == "!=")
        rewriteToConstInt(op, l != r);
      else if (op->symbol == "<")
        rewriteToConstInt(op, l < r);
      else if (op->symbol == "<=")
        rewriteToConstInt(op, l <= r);
      else
        changed = false;
    } else {
      if (op->symbol == "+")
        rewriteToConstFloat(op, l + r);
      else if (op->symbol == "-")
        rewriteToConstFloat(op, l - r);
      else if (op->symbol == "*")
        rewriteToConstFloat(op, l * r);
      else if (op->symbol == "/" && std::fabs(r) > 0.0)
        rewriteToConstFloat(op, l / r);
      else
        changed = false;
    }
    if (changed)
      stats.constFolded++;
    return changed;
  }
  return false;
}

bool Canonicalizer::simplifyStructuredControl(Op *op, CanonStats &stats) {
  if (!op)
    return false;

  if (op->kind == OpKind::If && !op->children.empty()) {
    Op *cond = op->children[0].get();
    if (isConstInt(cond) || isConstFloat(cond)) {
      bool truthy = takeTruthyBranch(cond);
      std::unique_ptr<Op> selected;
      if (truthy && op->children.size() >= 2)
        selected = std::move(op->children[1]);
      else if (!truthy && op->children.size() >= 3)
        selected = std::move(op->children[2]);

      op->kind = OpKind::Block;
      op->traits = defaultTraits(OpKind::Block);
      op->symbol.clear();
      op->children.clear();
      if (selected) {
        if (selected->kind == OpKind::Block) {
          for (auto &child : selected->children)
            op->children.push_back(std::move(child));
        } else {
          op->children.push_back(std::move(selected));
        }
      }
      stats.deadBranchesEliminated++;
      return true;
    }
  }

  if (op->kind == OpKind::While && !op->children.empty()) {
    Op *cond = op->children[0].get();
    if ((isConstInt(cond) && cond->intValue == 0) ||
        (isConstFloat(cond) && cond->floatValue == 0.0)) {
      op->kind = OpKind::Block;
      op->traits = defaultTraits(OpKind::Block);
      op->symbol.clear();
      op->children.clear();
      stats.deadBranchesEliminated++;
      return true;
    }
  }

  return false;
}

}  // namespace sys::hir
