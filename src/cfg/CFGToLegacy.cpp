#include "CFGToLegacy.h"

#include "CFGLegality.h"

namespace sys::cfg {

bool verifyCFGToLegacyLegality(const Module &cfgModule, ModuleOp *legacyModule, std::vector<std::string> &errors) {
  bool ok = true;
  std::vector<std::string> local;
  if (!verifyCFGLegalSet(cfgModule, local))
    ok = false;
  errors.insert(errors.end(), local.begin(), local.end());

  if (!legacyModule) {
    errors.push_back("cfg->legacy legality: legacy module is null");
    ok = false;
  }
  return ok;
}

std::unique_ptr<CodeGen> lowerToLegacyIR(const Module &cfgModule, ASTNode *fallbackAst, std::vector<std::string> &errors) {
  ASTNode *source = cfgModule.originAst ? cfgModule.originAst : fallbackAst;
  if (!source) {
    errors.push_back("cfg->legacy lowering: missing source ast");
    return nullptr;
  }

  auto cg = std::make_unique<CodeGen>(source);
  if (!verifyCFGToLegacyLegality(cfgModule, cg->getModule(), errors))
    return nullptr;
  return cg;
}

}  // namespace sys::cfg
