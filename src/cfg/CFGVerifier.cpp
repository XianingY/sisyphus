#include "CFGVerifier.h"

#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

namespace sys::cfg {

namespace {

std::string where(const Func &func, int bid) {
  std::ostringstream oss;
  oss << "func @" << func.name << " bb" << bid;
  return oss.str();
}

bool isIntLikeToken(const std::string &token, const std::unordered_set<std::string> &intValues) {
  if (token.empty())
    return false;
  if (token.rfind("f#", 0) == 0)
    return false;
  if (token[0] == '#')
    return true;
  return intValues.count(token);
}

}  // namespace

bool verify(const Module &module, std::vector<std::string> &errors) {
  errors.clear();
  if (module.funcs.empty()) {
    errors.push_back("cfg verifier: no function");
    return false;
  }

  bool ok = true;
  for (const auto &func : module.funcs) {
    for (const auto &param : func.params) {
      if (param.name.empty()) {
        errors.push_back("cfg verifier: func @" + func.name + " has unnamed parameter");
        ok = false;
      }
    }
    if (func.blocks.empty()) {
      errors.push_back("cfg verifier: func @" + func.name + " has no block");
      ok = false;
      continue;
    }
    if (func.entry < 0 || func.entry >= (int) func.blocks.size()) {
      errors.push_back("cfg verifier: invalid entry for func @" + func.name);
      ok = false;
      continue;
    }

    int n = (int) func.blocks.size();
    std::vector<std::set<int>> preds(n), succs(n);
    std::unordered_set<std::string> intValues;

    for (int bid = 0; bid < n; bid++) {
      const auto &bb = func.blocks[bid];
      if (bb.insts.empty()) {
        errors.push_back("cfg verifier: " + where(func, bid) + " is empty");
        ok = false;
        continue;
      }
      int firstNonPhi = -1;
      for (int i = 0; i < (int) bb.insts.size(); i++) {
        const auto &inst = bb.insts[i];
        if (inst.kind != OpKind::Phi && firstNonPhi == -1)
          firstNonPhi = i;
        if (inst.kind == OpKind::Phi && firstNonPhi != -1) {
          errors.push_back("cfg verifier: phi must appear before non-phi at " + where(func, bid));
          ok = false;
        }
        if ((inst.kind == OpKind::Load || inst.kind == OpKind::Store) && inst.memSize == 0) {
          errors.push_back("cfg verifier: load/store missing mem size at " + where(func, bid));
          ok = false;
        }
        if (inst.kind == OpKind::Call && inst.calleeArgTypes.size() != inst.args.size()) {
          errors.push_back("cfg verifier: call signature mismatch at " + where(func, bid));
          ok = false;
        }
        if (isTerminator(inst.kind) && i != (int) bb.insts.size() - 1) {
          errors.push_back("cfg verifier: terminator must be last at " + where(func, bid));
          ok = false;
        }
        if (!inst.result.empty() && inst.type == hir::TypeKind::Int)
          intValues.insert(inst.result);
      }

      const auto &term = bb.insts.back();
      if (!isTerminator(term.kind)) {
        errors.push_back("cfg verifier: missing terminator at " + where(func, bid));
        ok = false;
        continue;
      }
      if (term.kind == OpKind::Br) {
        if (term.targets.size() != 1) {
          errors.push_back("cfg verifier: br must have exactly one target at " + where(func, bid));
          ok = false;
        }
      }
      if (term.kind == OpKind::CondBr) {
        if (term.targets.size() != 2) {
          errors.push_back("cfg verifier: cond_br must have two targets at " + where(func, bid));
          ok = false;
        }
        if (term.args.empty() || !isIntLikeToken(term.args[0], intValues)) {
          errors.push_back("cfg verifier: cond_br condition must be int-like at " + where(func, bid));
          ok = false;
        }
      }

      for (int target : term.targets) {
        if (target < 0 || target >= n) {
          errors.push_back("cfg verifier: out-of-range target at " + where(func, bid));
          ok = false;
          continue;
        }
        succs[bid].insert(target);
        preds[target].insert(bid);
      }
    }

    for (int bid = 0; bid < n; bid++) {
      const auto &bb = func.blocks[bid];
      for (const auto &inst : bb.insts) {
        if (inst.kind != OpKind::Phi)
          continue;
        if (inst.phiPreds.size() != inst.args.size()) {
          errors.push_back("cfg verifier: phi incoming arity mismatch at " + where(func, bid));
          ok = false;
        }
        std::set<int> incoming(inst.phiPreds.begin(), inst.phiPreds.end());
        if (incoming != preds[bid] && !preds[bid].empty()) {
          errors.push_back("cfg verifier: phi incoming preds mismatch at " + where(func, bid));
          ok = false;
        }
      }
    }

    std::vector<int> reachable(n, 0);
    std::queue<int> q;
    q.push(func.entry);
    reachable[func.entry] = 1;
    while (!q.empty()) {
      int u = q.front();
      q.pop();
      for (int v : succs[u]) {
        if (reachable[v])
          continue;
        reachable[v] = 1;
        q.push(v);
      }
    }
    for (int bid = 0; bid < n; bid++) {
      if (reachable[bid])
        continue;
      for (const auto &inst : func.blocks[bid].insts) {
        if (inst.kind == OpKind::Phi) {
          errors.push_back("cfg verifier: unreachable block should not carry phi at " + where(func, bid));
          ok = false;
          break;
        }
      }
    }
  }
  return ok;
}

}  // namespace sys::cfg
