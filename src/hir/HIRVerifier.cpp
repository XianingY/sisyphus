#include "HIRVerifier.h"

#include <sstream>
#include <unordered_set>
#include <vector>

#include "../utils/DynamicCast.h"

namespace sys::hir {

namespace {

struct VerifyContext {
  std::vector<std::string> &errors;
  std::unordered_set<std::string> globals;
  std::unordered_set<std::string> functions;
  std::vector<std::unordered_set<std::string>> scopes;
};

void addError(VerifyContext &ctx, const Op *op, const std::string &msg) {
  std::ostringstream oss;
  oss << "[" << kindName(op ? op->kind : OpKind::Unknown) << "] " << msg;
  ctx.errors.push_back(oss.str());
}

void pushScope(VerifyContext &ctx) {
  ctx.scopes.emplace_back();
}

void popScope(VerifyContext &ctx) {
  if (!ctx.scopes.empty())
    ctx.scopes.pop_back();
}

void declareLocal(VerifyContext &ctx, const std::string &name) {
  if (ctx.scopes.empty())
    pushScope(ctx);
  ctx.scopes.back().insert(name);
}

void collectTopLevelSymbols(const Op *root, VerifyContext &ctx) {
  if (!root)
    return;
  for (const auto &child : root->children) {
    if (!child)
      continue;
    if (child->kind == OpKind::Func && !child->symbol.empty())
      ctx.functions.insert(child->symbol);
    if (child->kind == OpKind::VarDecl && !child->symbol.empty())
      ctx.globals.insert(child->symbol);
  }
}

bool verifyNode(const Op *op, VerifyContext &ctx, bool inLoop, const Op *parent) {
  if (!op) {
    ctx.errors.push_back("null op");
    return false;
  }

  bool ok = true;

  if ((op->traits & defaultTraits(op->kind)) != defaultTraits(op->kind)) {
    addError(ctx, op, "trait mismatch with op kind default");
    ok = false;
  }

  bool pushedScope = false;
  switch (op->kind) {
  case OpKind::Module:
    if (op->children.empty()) {
      addError(ctx, op, "module has no top-level ops");
      ok = false;
    }
    break;
  case OpKind::Func: {
    if (op->symbol.empty()) {
      addError(ctx, op, "function has empty symbol");
      ok = false;
    }
    if (op->children.empty() || op->children[0]->kind != OpKind::Block) {
      addError(ctx, op, "function must start with block");
      ok = false;
    }
    pushScope(ctx);
    pushedScope = true;
    if (op->origin && isa<FnDeclNode>(op->origin)) {
      auto *fn = cast<FnDeclNode>(op->origin);
      for (const auto &arg : fn->args)
        declareLocal(ctx, arg);
    }
    break;
  }
  case OpKind::Block:
    // Parser may emit single-statement block wrappers for declarations.
    // Keep lexical scopes for real blocks, but avoid creating a synthetic
    // nested scope for one-op wrappers under another block.
    if (!(parent && parent->kind == OpKind::Block && op->children.size() == 1)) {
      pushScope(ctx);
      pushedScope = true;
    }
    break;
  case OpKind::If:
    if (op->children.size() < 2) {
      addError(ctx, op, "if must have condition and then block");
      ok = false;
    }
    break;
  case OpKind::While:
    if (op->children.size() < 2) {
      addError(ctx, op, "while must have condition and body");
      ok = false;
    }
    break;
  case OpKind::For:
    if (op->children.size() < 4) {
      addError(ctx, op, "for must have init/cond/step/body");
      ok = false;
    }
    break;
  case OpKind::VarDecl:
    if (op->symbol.empty()) {
      addError(ctx, op, "variable declaration has empty symbol");
      ok = false;
    } else {
      declareLocal(ctx, op->symbol);
    }
    for (int dim : op->arrayDims) {
      if (dim <= 0) {
        addError(ctx, op, "array dimension must be positive");
        ok = false;
      }
    }
    break;
  case OpKind::Load:
  case OpKind::Store:
    if (op->symbol.empty()) {
      addError(ctx, op, "memory op has empty symbol");
      ok = false;
    }
    break;
  case OpKind::Call: {
    static const std::unordered_set<std::string> kBuiltins = {
      "getint", "getch", "getfloat", "getarray", "getfarray",
      "putint", "putch", "putfloat", "putarray", "putfarray",
      "starttime", "stoptime", "_sysy_starttime", "_sysy_stoptime"
    };
    if (op->symbol.empty()) {
      addError(ctx, op, "call has empty callee");
      ok = false;
    } else if (!ctx.functions.count(op->symbol) && !kBuiltins.count(op->symbol)) {
      addError(ctx, op, "unknown callee '" + op->symbol + "'");
      ok = false;
    }
    break;
  }
  case OpKind::Cmp:
    if (op->type != TypeKind::Int && op->type != TypeKind::Unknown) {
      addError(ctx, op, "cmp result type must be int");
      ok = false;
    }
    if (op->children.size() < 2) {
      addError(ctx, op, "cmp needs two operands");
      ok = false;
    }
    break;
  case OpKind::Arith:
    if (op->children.empty()) {
      addError(ctx, op, "arith has no operands");
      ok = false;
    }
    break;
  case OpKind::Break:
  case OpKind::Continue:
    if (!inLoop) {
      addError(ctx, op, "break/continue outside loop");
      ok = false;
    }
    break;
  case OpKind::Unknown:
    addError(ctx, op, "unknown op kind reached verifier");
    ok = false;
    break;
  case OpKind::Return:
  case OpKind::ConstInt:
  case OpKind::ConstFloat:
    break;
  }

  bool childLoop = inLoop || op->kind == OpKind::While || op->kind == OpKind::For;
  for (const auto &child : op->children)
    ok = verifyNode(child.get(), ctx, childLoop, op) && ok;

  if (pushedScope)
    popScope(ctx);

  return ok;
}

}  // namespace

bool verify(const Module &module, std::vector<std::string> &errors) {
  errors.clear();
  if (!module.root) {
    errors.push_back("hir module root is null");
    return false;
  }
  if (module.root->kind != OpKind::Module) {
    errors.push_back("hir root op must be module");
    return false;
  }

  VerifyContext ctx{errors, {}, {}, {}};
  collectTopLevelSymbols(module.root.get(), ctx);
  return verifyNode(module.root.get(), ctx, false, nullptr);
}

}  // namespace sys::hir
