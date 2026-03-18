#include "HIRToCFG.h"

#include <algorithm>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../utils/DynamicCast.h"

namespace sys::cfg {

namespace {

size_t productDims(const std::vector<int> &dims) {
  if (dims.empty())
    return 1;
  size_t prod = 1;
  for (int dim : dims)
    prod *= (size_t) std::max(dim, 1);
  return prod;
}

size_t typeSize(Type *ty);

size_t scalarSize(Type *ty) {
  if (!ty)
    return 4;
  if (isa<IntType>(ty) || isa<FloatType>(ty))
    return 4;
  if (isa<PointerType>(ty))
    return 8;
  if (isa<VoidType>(ty))
    return 0;
  if (isa<FunctionType>(ty))
    return 8;
  if (isa<ArrayType>(ty))
    return 8;
  return 4;
}

hir::TypeKind mapElementType(Type *ty) {
  if (!ty)
    return hir::TypeKind::Unknown;
  if (isa<IntType>(ty))
    return hir::TypeKind::Int;
  if (isa<FloatType>(ty))
    return hir::TypeKind::Float;
  if (auto ptr = dyn_cast<PointerType>(ty))
    return mapElementType(ptr->pointee);
  if (auto arr = dyn_cast<ArrayType>(ty))
    return mapElementType(arr->base);
  return hir::TypeKind::Unknown;
}

size_t typeSize(Type *ty) {
  if (!ty)
    return 4;
  if (auto arr = dyn_cast<ArrayType>(ty))
    return typeSize(arr->base) * productDims(arr->dims);
  return scalarSize(ty);
}

std::vector<int> typeDims(Type *ty) {
  if (auto arr = dyn_cast<ArrayType>(ty))
    return arr->dims;
  return {};
}

SymbolInfo buildSymbolInfo(const std::string &name, Type *ty, bool isGlobal, bool isParam, bool isMutable) {
  SymbolInfo info;
  info.name = name;
  info.type = hir::mapType(ty);
  info.elementType = mapElementType(ty);
  info.dims = typeDims(ty);
  info.isGlobal = isGlobal;
  info.isParam = isParam;
  info.isMutable = isMutable;
  info.elemSize = 4;
  if (info.elementType == hir::TypeKind::Float || info.elementType == hir::TypeKind::Int)
    info.elemSize = 4;
  else if (info.type == hir::TypeKind::Pointer || info.type == hir::TypeKind::Array)
    info.elemSize = 8;
  info.storageSize = typeSize(ty);
  if (info.storageSize == 0)
    info.storageSize = (info.type == hir::TypeKind::Pointer || info.type == hir::TypeKind::Array) ? 8 : 4;
  return info;
}

std::string formatFloat(double value) {
  std::ostringstream oss;
  oss << value;
  return oss.str();
}

bool literalToken(ASTNode *node, std::string &token) {
  if (!node)
    return false;
  if (auto x = dyn_cast<IntNode>(node)) {
    token = "#" + std::to_string(x->value);
    return true;
  }
  if (auto x = dyn_cast<FloatNode>(node)) {
    token = "f#" + formatFloat(x->value);
    return true;
  }
  if (auto u = dyn_cast<UnaryNode>(node)) {
    if (u->kind == UnaryNode::Minus) {
      std::string inner;
      if (!literalToken(u->node, inner))
        return false;
      if (inner.rfind("f#", 0) == 0) {
        token = "f#-" + inner.substr(2);
        return true;
      }
      if (inner.rfind("#", 0) == 0) {
        token = "#-" + inner.substr(1);
        return true;
      }
    }
  }
  return false;
}

struct FuncLoweringState {
  int tempId = 0;
  int blockId = 0;
};

class Lowerer {
  const hir::Module &hirModule;
  std::vector<std::string> &errors;

public:
  explicit Lowerer(const hir::Module &hirModule, std::vector<std::string> &errors):
    hirModule(hirModule), errors(errors) {}

  Module run() {
    Module cfgModule;
    cfgModule.originAst = hirModule.originAst;

    if (!hirModule.root || hirModule.root->kind != hir::OpKind::Module) {
      errors.push_back("hir->cfg: invalid HIR root");
      return cfgModule;
    }

    collectGlobals(*hirModule.root, cfgModule);

    for (const auto &child : hirModule.root->children) {
      if (!child)
        continue;
      if (child->kind != hir::OpKind::Func)
        continue;
      cfgModule.funcs.push_back(lowerFunc(*child, cfgModule));
    }

    if (cfgModule.funcs.empty())
      errors.push_back("hir->cfg: no function found");
    return cfgModule;
  }

private:
  static bool isTerminated(const Func &func, int bid) {
    if (bid < 0 || bid >= (int) func.blocks.size())
      return true;
    const auto &insts = func.blocks[bid].insts;
    if (insts.empty())
      return false;
    return isTerminator(insts.back().kind);
  }

  static hir::TypeKind inferExprType(const hir::Op *op) {
    if (!op)
      return hir::TypeKind::Unknown;
    if (op->kind == hir::OpKind::Cmp)
      return hir::TypeKind::Int;
    return op->type;
  }

  static size_t typeBytes(hir::TypeKind kind) {
    switch (kind) {
    case hir::TypeKind::Int:
    case hir::TypeKind::Float:
      return 4;
    case hir::TypeKind::Pointer:
    case hir::TypeKind::Array:
    case hir::TypeKind::Function:
      return 8;
    case hir::TypeKind::Void:
      return 0;
    case hir::TypeKind::Unknown:
      return 4;
    }
    return 4;
  }

  int newBlock(Func &func, FuncLoweringState &st, const std::string &prefix) {
    Block bb;
    bb.name = prefix + "." + std::to_string(st.blockId++);
    func.blocks.push_back(std::move(bb));
    return (int) func.blocks.size() - 1;
  }

  static void emit(Func &func, int bid, const Inst &inst) {
    if (bid < 0 || bid >= (int) func.blocks.size())
      return;
    func.blocks[bid].insts.push_back(inst);
  }

  void ensureTerminator(Func &func, int bid) {
    if (bid < 0 || bid >= (int) func.blocks.size())
      return;
    if (isTerminated(func, bid))
      return;
    Inst ret;
    ret.kind = OpKind::Ret;
    ret.args.push_back("#0");
    emit(func, bid, ret);
  }

  std::string newTemp(FuncLoweringState &st) {
    return "%t" + std::to_string(st.tempId++);
  }

  static std::unordered_map<std::string, SymbolInfo> toSymbolMap(const Func &func, const Module &module) {
    std::unordered_map<std::string, SymbolInfo> map;
    for (const auto &sym : module.globals)
      map[sym.name] = sym;
    for (const auto &sym : func.params)
      map[sym.name] = sym;
    for (const auto &sym : func.locals)
      map[sym.name] = sym;
    return map;
  }

  static void addStoreSym(std::set<std::string> *stores, const std::string &sym) {
    if (!stores || sym.empty())
      return;
    stores->insert(sym);
  }

  void collectGlobals(const hir::Op &root, Module &cfgModule) {
    for (const auto &child : root.children) {
      if (!child || child->kind != hir::OpKind::VarDecl)
        continue;
      const auto *origin = dyn_cast<VarDeclNode>(child->origin);
      Type *ty = origin ? origin->type : child->origin ? child->origin->type : nullptr;
      auto info = buildSymbolInfo(child->symbol, ty, true, false, origin ? origin->mut : true);

      if (origin && origin->init) {
        if (auto i = dyn_cast<IntNode>(origin->init)) {
          info.hasIntInit = true;
          info.intInit = i->value;
        } else if (auto f = dyn_cast<FloatNode>(origin->init)) {
          info.hasFloatInit = true;
          info.floatInit = f->value;
        } else if (auto arr = dyn_cast<ConstArrayNode>(origin->init)) {
          size_t elems = productDims(info.dims);
          if (elems == 0)
            elems = 1;
          if (arr->isFloat) {
            info.floatArrayInit.assign(arr->vf, arr->vf + elems);
          } else {
            info.intArrayInit.assign(arr->vi, arr->vi + elems);
          }
        }
      }

      cfgModule.globals.push_back(std::move(info));
    }
  }

  static void collectLocalsRec(const hir::Op *op, std::vector<SymbolInfo> &locals, std::unordered_set<std::string> &seen) {
    if (!op)
      return;
    if (op->kind == hir::OpKind::VarDecl && !op->symbol.empty() && !seen.count(op->symbol)) {
      const auto *origin = dyn_cast<VarDeclNode>(op->origin);
      Type *ty = origin ? origin->type : op->origin ? op->origin->type : nullptr;
      locals.push_back(buildSymbolInfo(op->symbol, ty, false, false, origin ? origin->mut : true));
      seen.insert(op->symbol);
    }
    for (const auto &child : op->children)
      collectLocalsRec(child.get(), locals, seen);
  }

  std::string lowerExpr(const hir::Op *op, Func &func, FuncLoweringState &st, int &cur,
                        const std::unordered_map<std::string, SymbolInfo> &symbols) {
    if (!op)
      return "#0";

    switch (op->kind) {
    case hir::OpKind::ConstInt:
      if (op->hasIntValue)
        return "#" + std::to_string(op->intValue);
      return "#0";
    case hir::OpKind::ConstFloat:
      if (op->hasFloatValue)
        return "f#" + formatFloat(op->floatValue);
      return "f#0.0";
    case hir::OpKind::Load: {
      Inst inst;
      inst.kind = OpKind::Load;
      inst.type = op->type;
      inst.symbol = op->symbol;
      auto it = symbols.find(op->symbol);
      if (it != symbols.end()) {
        inst.elementType = it->second.elementType;
        bool indexed = !op->children.empty();
        if (indexed) {
          inst.type = it->second.elementType;
          inst.memSize = it->second.elemSize;
        } else if (it->second.type == hir::TypeKind::Array || it->second.type == hir::TypeKind::Pointer) {
          inst.type = hir::TypeKind::Pointer;
          inst.memSize = 8;
        } else {
          inst.memSize = std::max((size_t) 4, it->second.storageSize);
        }
      } else {
        inst.memSize = typeBytes(inst.type);
      }
      inst.result = newTemp(st);
      for (const auto &idx : op->children)
        inst.args.push_back(lowerExpr(idx.get(), func, st, cur, symbols));
      emit(func, cur, inst);
      return inst.result;
    }
    case hir::OpKind::Call: {
      Inst inst;
      inst.kind = OpKind::Call;
      inst.type = op->type;
      inst.calleeRetType = op->type;
      inst.symbol = op->symbol;
      for (const auto &arg : op->children) {
        inst.calleeArgTypes.push_back(inferExprType(arg.get()));
        inst.args.push_back(lowerExpr(arg.get(), func, st, cur, symbols));
      }
      if (op->type != hir::TypeKind::Void)
        inst.result = newTemp(st);
      emit(func, cur, inst);
      return inst.result.empty() ? "#0" : inst.result;
    }
    case hir::OpKind::Arith:
    case hir::OpKind::Cmp: {
      Inst inst;
      inst.kind = (op->kind == hir::OpKind::Cmp) ? OpKind::Cmp : OpKind::Arith;
      inst.type = (op->kind == hir::OpKind::Cmp) ? hir::TypeKind::Int : op->type;
      if (op->kind == hir::OpKind::Cmp && !op->children.empty())
        inst.elementType = inferExprType(op->children[0].get());
      else
        inst.elementType = op->type;
      inst.symbol = op->symbol;
      inst.result = newTemp(st);
      for (const auto &child : op->children)
        inst.args.push_back(lowerExpr(child.get(), func, st, cur, symbols));
      emit(func, cur, inst);
      return inst.result;
    }
    default:
      break;
    }

    if (!op->children.empty())
      return lowerExpr(op->children[0].get(), func, st, cur, symbols);
    return "#0";
  }

  std::string normalizeCond(const hir::Op *cond, Func &func, FuncLoweringState &st, int &cur,
                            const std::unordered_map<std::string, SymbolInfo> &symbols) {
    auto value = lowerExpr(cond, func, st, cur, symbols);
    auto ty = inferExprType(cond);
    if (ty == hir::TypeKind::Int || ty == hir::TypeKind::Unknown)
      return value;

    Inst cmp;
    cmp.kind = OpKind::Cmp;
    cmp.type = hir::TypeKind::Int;
    cmp.symbol = "!=";
    cmp.result = newTemp(st);
    cmp.args.push_back(value);
    cmp.args.push_back(ty == hir::TypeKind::Float ? "f#0.0" : "#0");
    emit(func, cur, cmp);
    return cmp.result;
  }

  void emitLocalArrayInit(const hir::Op *op, const VarDeclNode *var, Func &func, int cur,
                          const std::unordered_map<std::string, SymbolInfo> &symbols,
                          std::set<std::string> *stores) {
    if (!var || !var->init)
      return;
    auto local = dyn_cast<LocalArrayNode>(var->init);
    auto arrTy = dyn_cast<ArrayType>(var->type);
    if (!local || !arrTy)
      return;

    size_t arrSize = std::max(1, arrTy->getSize());
    for (size_t i = 0; i < arrSize; i++) {
      std::string token;
      ASTNode *elem = local->va[i];
      if (!literalToken(elem, token)) {
        // Keep lowering robust on non-literal array initializers.
        token = (arrTy->base && isa<FloatType>(arrTy->base)) ? "f#0.0" : "#0";
      }
      Inst store;
      store.kind = OpKind::Store;
      store.symbol = op->symbol;
      store.args = { "#" + std::to_string(i), token };
      auto it = symbols.find(op->symbol);
      if (it != symbols.end()) {
        store.type = it->second.elementType;
        store.memSize = it->second.elemSize;
      } else {
        store.type = hir::TypeKind::Int;
        store.memSize = 4;
      }
      emit(func, cur, store);
      addStoreSym(stores, op->symbol);
    }
  }

  int lowerStmt(const hir::Op *op, Func &func, FuncLoweringState &st, int cur,
                int breakTarget, int continueTarget,
                const std::unordered_map<std::string, SymbolInfo> &symbols,
                std::set<std::string> *stores) {
    if (!op)
      return cur;

    if (isTerminated(func, cur)) {
      int cont = newBlock(func, st, "dead");
      cur = cont;
    }

    switch (op->kind) {
    case hir::OpKind::Block: {
      int at = cur;
      for (const auto &child : op->children)
        at = lowerStmt(child.get(), func, st, at, breakTarget, continueTarget, symbols, stores);
      return at;
    }
    case hir::OpKind::VarDecl: {
      auto it = symbols.find(op->symbol);
      bool isArray = it != symbols.end() && !it->second.dims.empty();
      if (isArray) {
        auto *var = dyn_cast<VarDeclNode>(op->origin);
        emitLocalArrayInit(op, var, func, cur, symbols, stores);
        return cur;
      }
      if (!op->children.empty()) {
        auto value = lowerExpr(op->children[0].get(), func, st, cur, symbols);
        Inst store;
        store.kind = OpKind::Store;
        store.symbol = op->symbol;
        store.args.push_back(value);
        if (it != symbols.end()) {
          store.type = it->second.type;
          store.memSize = std::max((size_t) 4, it->second.storageSize);
        }
        emit(func, cur, store);
        addStoreSym(stores, op->symbol);
      }
      return cur;
    }
    case hir::OpKind::Store: {
      if (op->children.empty())
        return cur;
      Inst store;
      store.kind = OpKind::Store;
      store.symbol = op->symbol;
      for (size_t i = 0; i + 1 < op->children.size(); i++)
        store.args.push_back(lowerExpr(op->children[i].get(), func, st, cur, symbols));
      store.args.push_back(lowerExpr(op->children.back().get(), func, st, cur, symbols));
      auto it = symbols.find(op->symbol);
      if (it != symbols.end()) {
        bool indexed = store.args.size() > 1;
        store.type = indexed ? it->second.elementType : it->second.type;
        store.memSize = indexed ? it->second.elemSize : std::max((size_t) 4, it->second.storageSize);
      }
      emit(func, cur, store);
      addStoreSym(stores, op->symbol);
      return cur;
    }
    case hir::OpKind::Call:
    case hir::OpKind::Arith:
    case hir::OpKind::Cmp:
    case hir::OpKind::Load:
      (void) lowerExpr(op, func, st, cur, symbols);
      return cur;
    case hir::OpKind::Return: {
      Inst ret;
      ret.kind = OpKind::Ret;
      if (!op->children.empty())
        ret.args.push_back(lowerExpr(op->children[0].get(), func, st, cur, symbols));
      else
        ret.args.push_back("#0");
      emit(func, cur, ret);
      return cur;
    }
    case hir::OpKind::Break: {
      if (breakTarget < 0) {
        errors.push_back("hir->cfg: break outside loop");
        return cur;
      }
      Inst br;
      br.kind = OpKind::Br;
      br.targets.push_back(breakTarget);
      emit(func, cur, br);
      return cur;
    }
    case hir::OpKind::Continue: {
      if (continueTarget < 0) {
        errors.push_back("hir->cfg: continue outside loop");
        return cur;
      }
      Inst br;
      br.kind = OpKind::Br;
      br.targets.push_back(continueTarget);
      emit(func, cur, br);
      return cur;
    }
    case hir::OpKind::If: {
      if (op->children.size() < 2) {
        errors.push_back("hir->cfg: malformed if op");
        return cur;
      }
      int thenId = newBlock(func, st, "if.then");
      int elseId = newBlock(func, st, "if.else");
      int mergeId = newBlock(func, st, "if.merge");

      auto cond = normalizeCond(op->children[0].get(), func, st, cur, symbols);
      Inst cbr;
      cbr.kind = OpKind::CondBr;
      cbr.args.push_back(cond);
      cbr.targets = { thenId, elseId };
      emit(func, cur, cbr);

      std::set<std::string> thenStores;
      int thenEnd = lowerStmt(op->children[1].get(), func, st, thenId, breakTarget, continueTarget, symbols, &thenStores);
      if (!isTerminated(func, thenEnd)) {
        Inst br;
        br.kind = OpKind::Br;
        br.targets.push_back(mergeId);
        emit(func, thenEnd, br);
      }

      std::set<std::string> elseStores;
      int elseEnd = elseId;
      if (op->children.size() >= 3)
        elseEnd = lowerStmt(op->children[2].get(), func, st, elseId, breakTarget, continueTarget, symbols, &elseStores);
      if (!isTerminated(func, elseEnd)) {
        Inst br;
        br.kind = OpKind::Br;
        br.targets.push_back(mergeId);
        emit(func, elseEnd, br);
      }

      std::vector<std::string> common;
      std::set_intersection(
        thenStores.begin(), thenStores.end(),
        elseStores.begin(), elseStores.end(),
        std::back_inserter(common));

      bool thenFlowsToMerge = false;
      bool elseFlowsToMerge = false;
      if (thenEnd >= 0 && thenEnd < (int) func.blocks.size() && !func.blocks[thenEnd].insts.empty()) {
        const auto &last = func.blocks[thenEnd].insts.back();
        thenFlowsToMerge = last.kind == OpKind::Br && !last.targets.empty() && last.targets[0] == mergeId;
      }
      if (elseEnd >= 0 && elseEnd < (int) func.blocks.size() && !func.blocks[elseEnd].insts.empty()) {
        const auto &last = func.blocks[elseEnd].insts.back();
        elseFlowsToMerge = last.kind == OpKind::Br && !last.targets.empty() && last.targets[0] == mergeId;
      }

      if (thenFlowsToMerge && elseFlowsToMerge) {
        for (const auto &sym : common) {
          Inst phi;
          phi.kind = OpKind::Phi;
          auto it = symbols.find(sym);
          phi.type = (it == symbols.end()) ? hir::TypeKind::Unknown : it->second.type;
          phi.elementType = (it == symbols.end()) ? hir::TypeKind::Unknown : it->second.elementType;
          phi.result = newTemp(st);
          phi.symbol = sym;
          phi.phiPreds = { thenEnd, elseEnd };
          phi.args = { "$" + sym + "@then", "$" + sym + "@else" };
          emit(func, mergeId, phi);

          Inst store;
          store.kind = OpKind::Store;
          store.symbol = sym;
          store.args = { phi.result };
          if (it != symbols.end()) {
            store.type = it->second.type;
            store.memSize = std::max((size_t) 4, it->second.storageSize);
          }
          emit(func, mergeId, store);
          addStoreSym(stores, sym);
        }
      }
      return mergeId;
    }
    case hir::OpKind::While: {
      if (op->children.size() < 2) {
        errors.push_back("hir->cfg: malformed while op");
        return cur;
      }
      int condId = newBlock(func, st, "while.cond");
      int bodyId = newBlock(func, st, "while.body");
      int exitId = newBlock(func, st, "while.exit");

      Inst jump;
      jump.kind = OpKind::Br;
      jump.targets.push_back(condId);
      emit(func, cur, jump);

      int condAt = condId;
      auto cond = normalizeCond(op->children[0].get(), func, st, condAt, symbols);
      Inst cbr;
      cbr.kind = OpKind::CondBr;
      cbr.args.push_back(cond);
      cbr.targets = { bodyId, exitId };
      emit(func, condAt, cbr);

      std::set<std::string> bodyStores;
      int bodyEnd = lowerStmt(op->children[1].get(), func, st, bodyId, exitId, condId, symbols, &bodyStores);
      if (!isTerminated(func, bodyEnd)) {
        Inst back;
        back.kind = OpKind::Br;
        back.targets.push_back(condId);
        emit(func, bodyEnd, back);
      }

      for (const auto &sym : bodyStores)
        addStoreSym(stores, sym);
      return exitId;
    }
    case hir::OpKind::For: {
      if (op->children.size() < 4) {
        errors.push_back("hir->cfg: malformed for op");
        return cur;
      }
      cur = lowerStmt(op->children[0].get(), func, st, cur, breakTarget, continueTarget, symbols, stores);

      int condId = newBlock(func, st, "for.cond");
      int bodyId = newBlock(func, st, "for.body");
      int stepId = newBlock(func, st, "for.step");
      int exitId = newBlock(func, st, "for.exit");

      Inst toCond;
      toCond.kind = OpKind::Br;
      toCond.targets.push_back(condId);
      emit(func, cur, toCond);

      int condAt = condId;
      auto cond = normalizeCond(op->children[1].get(), func, st, condAt, symbols);
      Inst cbr;
      cbr.kind = OpKind::CondBr;
      cbr.args.push_back(cond);
      cbr.targets = { bodyId, exitId };
      emit(func, condAt, cbr);

      int bodyEnd = lowerStmt(op->children[3].get(), func, st, bodyId, exitId, stepId, symbols, stores);
      if (!isTerminated(func, bodyEnd)) {
        Inst toStep;
        toStep.kind = OpKind::Br;
        toStep.targets.push_back(stepId);
        emit(func, bodyEnd, toStep);
      }

      int stepEnd = lowerStmt(op->children[2].get(), func, st, stepId, exitId, stepId, symbols, stores);
      if (!isTerminated(func, stepEnd)) {
        Inst back;
        back.kind = OpKind::Br;
        back.targets.push_back(condId);
        emit(func, stepEnd, back);
      }
      return exitId;
    }
    default:
      return cur;
    }
  }

  Func lowerFunc(const hir::Op &funcOp, const Module &cfgModule) {
    Func func;
    FuncLoweringState st;
    func.name = funcOp.symbol.empty() ? "anonymous" : funcOp.symbol;

    if (auto *fn = dyn_cast<FnDeclNode>(funcOp.origin)) {
      if (auto *fnTy = dyn_cast<FunctionType>(fn->type)) {
        func.returnType = hir::mapType(fnTy->ret);
        for (size_t i = 0; i < fn->args.size(); i++) {
          Type *argTy = i < fnTy->params.size() ? fnTy->params[i] : nullptr;
          func.params.push_back(buildSymbolInfo(fn->args[i], argTy, false, true, true));
        }
      }
    }

    std::unordered_set<std::string> seen;
    for (const auto &param : func.params)
      seen.insert(param.name);
    if (!funcOp.children.empty())
      collectLocalsRec(funcOp.children[0].get(), func.locals, seen);

    func.entry = newBlock(func, st, "entry");

    auto symbols = toSymbolMap(func, cfgModule);

    int cur = func.entry;
    if (!funcOp.children.empty())
      cur = lowerStmt(funcOp.children[0].get(), func, st, cur, -1, -1, symbols, nullptr);
    ensureTerminator(func, cur);

    for (int bid = 0; bid < (int) func.blocks.size(); bid++)
      ensureTerminator(func, bid);
    return func;
  }
};

}  // namespace

Module lowerFromHIR(const hir::Module &hirModule, std::vector<std::string> &errors) {
  Lowerer lowering(hirModule, errors);
  return lowering.run();
}

}  // namespace sys::cfg
