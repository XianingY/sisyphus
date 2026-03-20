#include "Sema.h"
#include "ASTNode.h"
#include "../utils/DynamicCast.h"
#include "Type.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <set>
#include <sstream>

using namespace sys;

PointerType *Sema::decay(ArrayType *arrTy) {
  std::vector<int> dims;
  for (int i = 1; i < arrTy->dims.size(); i++)
    dims.push_back(arrTy->dims[i]);
  if (!dims.size())
    return ctx.create<PointerType>(arrTy->base);
  return ctx.create<PointerType>(ctx.create<ArrayType>(arrTy->base, dims));
}

ArrayType *Sema::raise(PointerType *ptr) {
  std::vector<int> dims { 1 };
  Type *base;
  if (auto pointee = dyn_cast<ArrayType>(ptr->pointee)) {
    for (auto x : pointee->dims)
      dims.push_back(x);
    base = pointee->base;
  } else
    base = ptr->pointee;
  return ctx.create<ArrayType>(base, dims);
}

[[noreturn]] void Sema::fail(const std::string &msg) {
  throw CompileError("sema error: " + msg);
}

void Sema::declareSymbol(const std::string &name, Type *ty, bool isMutable) {
  if (scopeDecls.empty())
    scopeDecls.emplace_back();
  auto &cur = scopeDecls.back();
  if (cur.count(name))
    fail("duplicate declaration in same scope: " + name);
  cur.insert(name);
  symbols[name] = ty;
  mutableSymbols[name] = isMutable;
}

bool Sema::isMutableSymbol(const std::string &name) const {
  auto it = mutableSymbols.find(name);
  if (it == mutableSymbols.end())
    return true;
  return it->second;
}

Type *Sema::infer(ASTNode *node) {
  if (auto fn = dyn_cast<FnDeclNode>(node)) {
    assert(fn->type);
    auto fnTy = cast<FunctionType>(fn->type);
    declareSymbol(fn->name, fn->type, true);
    currentFunc = fnTy;

    SemanticScope scope(*this);
    for (int i = 0; i < fn->args.size(); i++) {
      declareSymbol(fn->args[i], fnTy->params[i], true);
    }

    for (auto x : fn->body->nodes)
      infer(x);
    return ctx.create<VoidType>();
  }

  if (auto block = dyn_cast<BlockNode>(node)) {
    SemanticScope scope(*this);
    for (auto x : block->nodes)
      infer(x);
    return node->type = ctx.create<VoidType>();
  }

  if (auto block = dyn_cast<TransparentBlockNode>(node)) {
    for (auto x : block->nodes)
      infer(x);
    return node->type = ctx.create<VoidType>();
  }

  if (isa<IntNode>(node))
    return node->type = ctx.create<IntType>();

  if (isa<FloatNode>(node))
    return node->type = ctx.create<FloatType>();

  if (isa<BreakNode>(node) || isa<ContinueNode>(node) || isa<EmptyNode>(node))
    return node->type = ctx.create<VoidType>();
  
  if (auto binary = dyn_cast<BinaryNode>(node)) {
    auto lty = infer(binary->l);
    auto rty = infer(binary->r);
    if (isa<VoidType>(lty) || isa<VoidType>(rty))
      fail("void value used in binary expression");

    // Special: we need to convert float to comparison with zero.
    if (binary->kind == BinaryNode::And || binary->kind == BinaryNode::Or) {
      if (isa<FloatType>(lty)) {
        auto zero = new FloatNode(0);
        zero->type = ctx.create<FloatType>();

        auto ne = new BinaryNode(BinaryNode::Ne, binary->l, zero);
        ne->type = ctx.create<IntType>();

        binary->l = ne;
      }
      if (isa<FloatType>(rty)) {
        auto zero = new FloatNode(0);
        zero->type = ctx.create<FloatType>();

        auto ne = new BinaryNode(BinaryNode::Ne, binary->r, zero);
        ne->type = ctx.create<IntType>();

        binary->r = ne;
      }
      return node->type = ctx.create<IntType>();
    }

    if (isa<FloatType>(lty) && isa<IntType>(rty)) {
      binary->r = new UnaryNode(UnaryNode::Int2Float, binary->r);
      rty = binary->r->type = ctx.create<FloatType>();
    }

    if (isa<IntType>(lty) && isa<FloatType>(rty)) {
      binary->l = new UnaryNode(UnaryNode::Int2Float, binary->l);
      lty = binary->l->type = ctx.create<FloatType>();
    }

    std::set<decltype(binary->kind)> intops {
      BinaryNode::And,
      BinaryNode::Or,
      BinaryNode::Eq,
      BinaryNode::Ne,
      BinaryNode::Le,
      BinaryNode::Lt,
    };
    if (isa<FloatType>(lty) && isa<FloatType>(rty) && !intops.count(binary->kind))
      return node->type = ctx.create<FloatType>();

    if (lty != rty) {
      fail("incompatible operand types in binary operation");
    }
    
    return node->type = ctx.create<IntType>();
  }

  if (auto unary = dyn_cast<UnaryNode>(node)) {
    auto ty = infer(unary->node);
    if (isa<VoidType>(ty))
      fail("void value used in unary expression");
    // These two ops won't be emitted in parser.
    assert(unary->kind != UnaryNode::Float2Int && unary->kind != UnaryNode::Int2Float);

    if (isa<FloatType>(ty) && unary->kind == UnaryNode::Minus)
      return node->type = ctx.create<FloatType>();
    
    return node->type = ctx.create<IntType>();
  }

  if (auto vardecl = dyn_cast<VarDeclNode>(node)) {
    assert(node->type);
    declareSymbol(vardecl->name, node->type, vardecl->mut);
    if (!vardecl->init)
      return ctx.create<VoidType>();

    if (vardecl->global || !vardecl->mut)
      // Already folded. Just propagate type.
      vardecl->init->type = node->type;
    else {
      auto ty = infer(vardecl->init);
      if (isa<IntType>(ty) && isa<FloatType>(vardecl->type)) {
        vardecl->init = new UnaryNode(UnaryNode::Int2Float, vardecl->init);
        vardecl->init->type = ctx.create<FloatType>();
        return ctx.create<VoidType>();
      }

      if (isa<FloatType>(ty) && isa<IntType>(vardecl->type)) {
        vardecl->init = new UnaryNode(UnaryNode::Float2Int, vardecl->init);
        vardecl->init->type = ctx.create<IntType>();
        return ctx.create<VoidType>();
      }

      if (ty != vardecl->type) {
        fail("incompatible initializer type");
      }
    }
    return ctx.create<VoidType>();
  }

  if (auto ret = dyn_cast<ReturnNode>(node)) {
    auto retTy = cast<FunctionType>(currentFunc)->ret;
    if (!ret->node) {
      if (!isa<VoidType>(retTy))
        fail("non-void function must return a value");
      return ctx.create<VoidType>();
    }
    if (isa<VoidType>(retTy))
      fail("void function cannot return a value");

    auto ty = infer(ret->node);
    if (isa<IntType>(retTy) && isa<FloatType>(ty)) {
      ret->node = new UnaryNode(UnaryNode::Float2Int, ret->node);
      ret->node->type = ctx.create<IntType>();
      return ctx.create<VoidType>();
    }

    if (isa<FloatType>(retTy) && isa<IntType>(ty)) {
      ret->node = new UnaryNode(UnaryNode::Int2Float, ret->node);
      ret->node->type = ctx.create<FloatType>();
      return ctx.create<VoidType>();
    }

    if (retTy != ret->node->type) {
      fail("return type mismatch");
    }
    return ctx.create<VoidType>();
  }
  
  if (auto ref = dyn_cast<VarRefNode>(node)) {
    if (!symbols.count(ref->name)) {
      fail("cannot find symbol " + ref->name);
    }
    auto ty = symbols[ref->name];

    // Decay.
    if (auto arrTy = dyn_cast<ArrayType>(ty))
      return node->type = decay(arrTy);
    
    return node->type = symbols[ref->name];
  }

  if (auto branch = dyn_cast<IfNode>(node)) {
    auto condTy = infer(branch->cond);
    if (isa<FloatType>(condTy)) {
      // We should insert a check of whether it's zero.
      // Truncation won't work; if (0.2) ... should be evaluated to true.
      auto zero = new FloatNode(0);
      zero->type = ctx.create<FloatType>();

      auto ne = new BinaryNode(BinaryNode::Ne, branch->cond, zero);
      ne->type = ctx.create<IntType>();

      branch->cond = ne;
    }
    
    infer(branch->ifso);
    if (branch->ifnot)
      infer(branch->ifnot);
    return node->type = ctx.create<VoidType>();
  }

  if (auto loop = dyn_cast<WhileNode>(node)) {
    auto condTy = infer(loop->cond);
    if (isa<FloatType>(condTy)) {
      auto zero = new FloatNode(0);
      zero->type = ctx.create<FloatType>();
      auto ne = new BinaryNode(BinaryNode::Ne, loop->cond, zero);
      ne->type = ctx.create<IntType>();
      loop->cond = ne;
    } else if (!isa<IntType>(condTy)) {
      fail("while condition must be int/float");
    }
    infer(loop->body);
    return node->type = ctx.create<VoidType>();
  }

  if (auto assign = dyn_cast<AssignNode>(node)) {
    auto lref = dyn_cast<VarRefNode>(assign->l);
    if (!lref)
      fail("left side of assignment must be variable");
    if (!isMutableSymbol(lref->name))
      fail("cannot assign to const variable " + lref->name);

    auto lty = infer(assign->l);
    auto rty = infer(assign->r);
    if (isa<VoidType>(rty))
      fail("void value cannot be assigned");
    if (isa<IntType>(lty) && isa<FloatType>(rty)) {
      assign->r = new UnaryNode(UnaryNode::Float2Int, assign->r);
      assign->r->type = ctx.create<IntType>();
      return ctx.create<VoidType>();
    }

    if (isa<FloatType>(lty) && isa<IntType>(rty)) {
      assign->r = new UnaryNode(UnaryNode::Int2Float, assign->r);
      assign->r->type = ctx.create<FloatType>();
      return ctx.create<VoidType>();
    }

    if (lty != rty) {
      fail("assignment type mismatch");
    }
    return node->type = ctx.create<VoidType>();
  }

  if (isa<ConstArrayNode>(node)) {
    assert(node->type);
    return node->type;
  }

  if (auto arr = dyn_cast<LocalArrayNode>(node)) {
    assert(node->type);
    auto arrTy = cast<ArrayType>(node->type);
    auto baseTy = arrTy->base;
    auto size = arrTy->getSize();
    for (int i = 0; i < size; i++) {
      auto &x = arr->va[i];
      if (!x)
        continue;
      auto ty = infer(x);

      if (isa<FloatType>(ty) && isa<IntType>(baseTy)) {
        x = new UnaryNode(UnaryNode::Float2Int, x);
        x->type = ctx.create<IntType>();
        continue;
      }

      if (isa<IntType>(ty) && isa<FloatType>(baseTy)) {
        x = new UnaryNode(UnaryNode::Int2Float, x);
        x->type = ctx.create<FloatType>();
        continue;
      }
    }
    return node->type;
  }

  if (auto call = dyn_cast<CallNode>(node)) {
    if (!symbols.count(call->func))
      fail("cannot find function " + call->func);
    auto *symTy = symbols[call->func];
    if (!isa<FunctionType>(symTy))
      fail(call->func + " is not a function");
    auto fnTy = cast<FunctionType>(symTy);
    if (call->args.size() != fnTy->params.size()) {
      std::ostringstream os;
      os << "function argument count mismatch for " << call->func
         << ": expected " << fnTy->params.size()
         << ", got " << call->args.size();
      fail(os.str());
    }
    for (size_t i = 0; i < fnTy->params.size(); i++) {
      ASTNode *&x = call->args[i];
      auto ty = infer(x);
      auto argTy = fnTy->params[i];
      if (isa<VoidType>(ty))
        fail("void value cannot be used as function argument");

      if (isa<FloatType>(ty) && isa<IntType>(argTy)) {
        x = new UnaryNode(UnaryNode::Float2Int, x);
        x->type = ctx.create<IntType>();
        continue;
      }

      if (isa<IntType>(ty) && isa<FloatType>(argTy)) {
        x = new UnaryNode(UnaryNode::Int2Float, x);
        x->type = ctx.create<FloatType>();
        continue;
      }

      // Both pointers / arrays.
    }
    return node->type = fnTy->ret;
  }

  if (auto access = dyn_cast<ArrayAccessNode>(node)) {
    if (!symbols.count(access->array))
      fail("cannot find array symbol " + access->array);
    auto realTy = symbols[access->array];
    ArrayType *arrTy;
    if (isa<ArrayType>(realTy))
      arrTy = cast<ArrayType>(realTy);
    else if (isa<PointerType>(realTy))
      arrTy = raise(cast<PointerType>(realTy));
    else
      fail(access->array + " is not array/pointer");
    
    access->arrTy = arrTy;
    std::vector<int> dimsNew;
    for (int i = access->indices.size(); i < arrTy->dims.size(); i++)
      dimsNew.push_back(arrTy->dims[i]);
    
    for (auto x : access->indices) {
      auto ty = infer(x);
      if (!isa<IntType>(ty))
        fail("array index must be int");
    }

    auto resultTy = dimsNew.size()
      ? (Type*) decay(ctx.create<ArrayType>(arrTy->base, dimsNew))
      : arrTy->base;
    return node->type = resultTy;
  }

  if (auto write = dyn_cast<ArrayAssignNode>(node)) {
    if (!symbols.count(write->array))
      fail("cannot find array symbol " + write->array);
    auto realTy = symbols[write->array];
    ArrayType *arrTy;
    if (isa<ArrayType>(realTy))
      arrTy = cast<ArrayType>(realTy);
    else if (isa<PointerType>(realTy))
      arrTy = raise(cast<PointerType>(realTy));
    else
      fail(write->array + " is not array/pointer");

    auto baseTy = arrTy->base;
    if (write->indices.size() != arrTy->dims.size())
      fail("array assignment index rank mismatch");
    write->arrTy = arrTy;

    for (auto x : write->indices) {
      auto ty = infer(x);
      if (!isa<IntType>(ty))
        fail("array index must be int");
    }

    auto valueTy = infer(write->value);
    if (isa<VoidType>(valueTy))
      fail("void value cannot be assigned to array element");

    if (isa<FloatType>(baseTy) && isa<IntType>(valueTy)) {
      write->value = new UnaryNode(UnaryNode::Int2Float, write->value);
      write->value->type = ctx.create<FloatType>();
    }

    if (isa<IntType>(baseTy) && isa<FloatType>(valueTy)) {
      write->value = new UnaryNode(UnaryNode::Float2Int, write->value);
      write->value->type = ctx.create<IntType>();
    }

    return node->type = ctx.create<VoidType>();
  }

  fail("cannot infer AST node kind");
}

Sema::Sema(ASTNode *node, TypeContext &ctx): ctx(ctx) {
  auto intTy = ctx.create<IntType>();
  auto floatTy = ctx.create<FloatType>();
  auto voidTy = ctx.create<VoidType>();
  auto intPtrTy = ctx.create<ArrayType>(intTy, std::vector { 1 });
  auto floatPtrTy = ctx.create<ArrayType>(floatTy, std::vector { 1 });

  using Args = std::vector<Type*>;
  Args empty;
  
  // Internal library.
  symbols = {
    { "getint", ctx.create<FunctionType>(intTy, empty) },
    { "getch", ctx.create<FunctionType>(intTy, empty) },
    { "getfloat", ctx.create<FunctionType>(floatTy, empty) },
    { "getarray", ctx.create<FunctionType>(intTy, Args { intPtrTy }) },
    { "getfarray", ctx.create<FunctionType>(intTy, Args { floatPtrTy } ) },
    { "putint", ctx.create<FunctionType>(voidTy, Args { intTy }) },
    { "putch", ctx.create<FunctionType>(voidTy, Args { intTy }) },
    { "putfloat", ctx.create<FunctionType>(voidTy, Args { floatTy }) },
    { "putarray", ctx.create<FunctionType>(voidTy, Args { intTy, intPtrTy }) },
    { "putfarray", ctx.create<FunctionType>(voidTy, Args { intTy, floatPtrTy }) },
    { "_sysy_starttime", ctx.create<FunctionType>(voidTy, Args { intTy }) },
    { "_sysy_stoptime", ctx.create<FunctionType>(voidTy, Args { intTy }) },
  };
  for (const auto &it : symbols) {
    mutableSymbols[it.first] = true;
  }

  infer(node);
}
