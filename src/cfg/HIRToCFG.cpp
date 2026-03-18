#include "HIRToCFG.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>

namespace sys::cfg {

namespace {

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

    for (const auto &child : hirModule.root->children) {
      if (!child)
        continue;
      if (child->kind != hir::OpKind::Func)
        continue;
      cfgModule.funcs.push_back(lowerFunc(*child));
    }

    if (cfgModule.funcs.empty())
      errors.push_back("hir->cfg: no function found");
    return cfgModule;
  }

private:
  static std::string formatFloat(double value) {
    std::string raw = std::to_string(value);
    while (!raw.empty() && raw.back() == '0')
      raw.pop_back();
    if (!raw.empty() && raw.back() == '.')
      raw.push_back('0');
    if (raw.empty())
      raw = "0.0";
    return raw;
  }

  int newBlock(Func &func, FuncLoweringState &st, const std::string &prefix) {
    Block bb;
    bb.name = prefix + "." + std::to_string(st.blockId++);
    func.blocks.push_back(std::move(bb));
    return (int) func.blocks.size() - 1;
  }

  std::string newTemp(FuncLoweringState &st) {
    return "%t" + std::to_string(st.tempId++);
  }

  bool isTerminated(const Func &func, int bid) const {
    if (bid < 0 || bid >= (int) func.blocks.size())
      return true;
    const auto &insts = func.blocks[bid].insts;
    if (insts.empty())
      return false;
    return isTerminator(insts.back().kind);
  }

  void emit(Func &func, int bid, const Inst &inst) {
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

  hir::TypeKind inferExprType(const hir::Op *op) const {
    if (!op)
      return hir::TypeKind::Unknown;
    if (op->kind == hir::OpKind::Cmp)
      return hir::TypeKind::Int;
    return op->type;
  }

  std::string lowerExpr(const hir::Op *op, Func &func, FuncLoweringState &st, int &cur) {
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
      inst.result = newTemp(st);
      for (const auto &idx : op->children)
        inst.args.push_back(lowerExpr(idx.get(), func, st, cur));
      emit(func, cur, inst);
      return inst.result;
    }
    case hir::OpKind::Call: {
      Inst inst;
      inst.kind = OpKind::Call;
      inst.type = op->type;
      inst.symbol = op->symbol;
      for (const auto &arg : op->children)
        inst.args.push_back(lowerExpr(arg.get(), func, st, cur));
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
      inst.symbol = op->symbol;
      inst.result = newTemp(st);
      for (const auto &child : op->children)
        inst.args.push_back(lowerExpr(child.get(), func, st, cur));
      emit(func, cur, inst);
      return inst.result;
    }
    default:
      break;
    }

    if (!op->children.empty())
      return lowerExpr(op->children[0].get(), func, st, cur);
    return "#0";
  }

  std::string normalizeCond(const hir::Op *cond, Func &func, FuncLoweringState &st, int &cur) {
    auto value = lowerExpr(cond, func, st, cur);
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

  void addStoreSym(std::set<std::string> *stores, const std::string &sym) {
    if (!stores || sym.empty())
      return;
    stores->insert(sym);
  }

  int lowerStmt(const hir::Op *op, Func &func, FuncLoweringState &st, int cur, int breakTarget, int continueTarget, std::set<std::string> *stores) {
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
        at = lowerStmt(child.get(), func, st, at, breakTarget, continueTarget, stores);
      return at;
    }
    case hir::OpKind::VarDecl: {
      if (!op->children.empty()) {
        auto value = lowerExpr(op->children[0].get(), func, st, cur);
        Inst store;
        store.kind = OpKind::Store;
        store.symbol = op->symbol;
        store.args.push_back(value);
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
        store.args.push_back(lowerExpr(op->children[i].get(), func, st, cur));
      store.args.push_back(lowerExpr(op->children.back().get(), func, st, cur));
      emit(func, cur, store);
      addStoreSym(stores, op->symbol);
      return cur;
    }
    case hir::OpKind::Call:
    case hir::OpKind::Arith:
    case hir::OpKind::Cmp:
    case hir::OpKind::Load:
      (void) lowerExpr(op, func, st, cur);
      return cur;
    case hir::OpKind::Return: {
      Inst ret;
      ret.kind = OpKind::Ret;
      if (!op->children.empty())
        ret.args.push_back(lowerExpr(op->children[0].get(), func, st, cur));
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

      auto cond = normalizeCond(op->children[0].get(), func, st, cur);
      Inst cbr;
      cbr.kind = OpKind::CondBr;
      cbr.args.push_back(cond);
      cbr.targets = { thenId, elseId };
      emit(func, cur, cbr);

      std::set<std::string> thenStores;
      int thenEnd = lowerStmt(op->children[1].get(), func, st, thenId, breakTarget, continueTarget, &thenStores);
      if (!isTerminated(func, thenEnd)) {
        Inst br;
        br.kind = OpKind::Br;
        br.targets.push_back(mergeId);
        emit(func, thenEnd, br);
      }

      std::set<std::string> elseStores;
      int elseEnd = elseId;
      if (op->children.size() >= 3)
        elseEnd = lowerStmt(op->children[2].get(), func, st, elseId, breakTarget, continueTarget, &elseStores);
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

      std::vector<std::pair<std::string, std::string>> phiPairs;
      if (thenFlowsToMerge && elseFlowsToMerge) {
        for (const auto &sym : common) {
          Inst phi;
          phi.kind = OpKind::Phi;
          phi.result = newTemp(st);
          phi.symbol = sym;
          phi.phiPreds = { thenEnd, elseEnd };
          phi.args = { "$" + sym + "@then", "$" + sym + "@else" };
          emit(func, mergeId, phi);
          phiPairs.emplace_back(sym, phi.result);
        }
      }
      for (const auto &it : phiPairs) {
        Inst store;
        store.kind = OpKind::Store;
        store.symbol = it.first;
        store.args = { it.second };
        emit(func, mergeId, store);
        addStoreSym(stores, it.first);
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
      auto cond = normalizeCond(op->children[0].get(), func, st, condAt);
      Inst cbr;
      cbr.kind = OpKind::CondBr;
      cbr.args.push_back(cond);
      cbr.targets = { bodyId, exitId };
      emit(func, condAt, cbr);

      std::set<std::string> bodyStores;
      int bodyEnd = lowerStmt(op->children[1].get(), func, st, bodyId, exitId, condId, &bodyStores);
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
      cur = lowerStmt(op->children[0].get(), func, st, cur, breakTarget, continueTarget, stores);

      int condId = newBlock(func, st, "for.cond");
      int bodyId = newBlock(func, st, "for.body");
      int stepId = newBlock(func, st, "for.step");
      int exitId = newBlock(func, st, "for.exit");

      Inst toCond;
      toCond.kind = OpKind::Br;
      toCond.targets.push_back(condId);
      emit(func, cur, toCond);

      int condAt = condId;
      auto cond = normalizeCond(op->children[1].get(), func, st, condAt);
      Inst cbr;
      cbr.kind = OpKind::CondBr;
      cbr.args.push_back(cond);
      cbr.targets = { bodyId, exitId };
      emit(func, condAt, cbr);

      int bodyEnd = lowerStmt(op->children[3].get(), func, st, bodyId, exitId, stepId, stores);
      if (!isTerminated(func, bodyEnd)) {
        Inst toStep;
        toStep.kind = OpKind::Br;
        toStep.targets.push_back(stepId);
        emit(func, bodyEnd, toStep);
      }

      int stepEnd = lowerStmt(op->children[2].get(), func, st, stepId, exitId, stepId, stores);
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

  Func lowerFunc(const hir::Op &funcOp) {
    Func func;
    FuncLoweringState st;
    func.name = funcOp.symbol.empty() ? "anonymous" : funcOp.symbol;
    func.entry = newBlock(func, st, "entry");

    int cur = func.entry;
    if (!funcOp.children.empty())
      cur = lowerStmt(funcOp.children[0].get(), func, st, cur, -1, -1, nullptr);
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
