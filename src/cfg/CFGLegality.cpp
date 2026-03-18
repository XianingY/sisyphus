#include "CFGLegality.h"

#include <set>

namespace sys::cfg {

namespace {

bool isLegalHIRKind(hir::OpKind kind) {
  switch (kind) {
  case hir::OpKind::Module:
  case hir::OpKind::Func:
  case hir::OpKind::Block:
  case hir::OpKind::If:
  case hir::OpKind::While:
  case hir::OpKind::For:
  case hir::OpKind::Call:
  case hir::OpKind::Load:
  case hir::OpKind::Store:
  case hir::OpKind::Arith:
  case hir::OpKind::Cmp:
  case hir::OpKind::Return:
  case hir::OpKind::VarDecl:
  case hir::OpKind::Break:
  case hir::OpKind::Continue:
  case hir::OpKind::ConstInt:
  case hir::OpKind::ConstFloat:
    return true;
  case hir::OpKind::Unknown:
    return false;
  }
  return false;
}

bool isLegalCFGKind(OpKind kind) {
  switch (kind) {
  case OpKind::Nop:
  case OpKind::Call:
  case OpKind::Load:
  case OpKind::Store:
  case OpKind::Arith:
  case OpKind::Cmp:
  case OpKind::Phi:
  case OpKind::Ret:
  case OpKind::Br:
  case OpKind::CondBr:
    return true;
  }
  return false;
}

bool visitHIR(const hir::Op *op, std::vector<std::string> &errors) {
  if (!op)
    return true;
  bool ok = true;
  if (!isLegalHIRKind(op->kind)) {
    errors.push_back("hir legality: illegal op kind");
    ok = false;
  }
  for (const auto &child : op->children)
    ok = visitHIR(child.get(), errors) && ok;
  return ok;
}

}  // namespace

bool verifyHIRLegalSet(const hir::Module &module, std::vector<std::string> &errors) {
  errors.clear();
  if (!module.root) {
    errors.push_back("hir legality: null module root");
    return false;
  }
  return visitHIR(module.root.get(), errors);
}

bool verifyCFGLegalSet(const Module &module, std::vector<std::string> &errors) {
  bool ok = true;
  if (module.funcs.empty()) {
    errors.push_back("cfg legality: no function");
    return false;
  }

  for (const auto &func : module.funcs) {
    for (const auto &bb : func.blocks) {
      for (const auto &inst : bb.insts) {
        if (!isLegalCFGKind(inst.kind)) {
          errors.push_back("cfg legality: illegal inst kind in func @" + func.name);
          ok = false;
        }
      }
    }
  }
  return ok;
}

bool verifyHIRToCFGConversion(const hir::Module &hirModule, const Module &cfgModule, std::vector<std::string> &errors) {
  bool ok = true;
  std::vector<std::string> local;
  if (!verifyHIRLegalSet(hirModule, local))
    ok = false;
  errors.insert(errors.end(), local.begin(), local.end());
  local.clear();
  if (!verifyCFGLegalSet(cfgModule, local))
    ok = false;
  errors.insert(errors.end(), local.begin(), local.end());
  if (cfgModule.funcs.empty()) {
    errors.push_back("hir->cfg legality: empty cfg module");
    ok = false;
  }
  return ok;
}

}  // namespace sys::cfg
