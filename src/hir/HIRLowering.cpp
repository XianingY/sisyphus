#include "HIRLowering.h"

namespace sys::hir {

std::unique_ptr<CodeGen> lowerToLegacyIR(const Module &module, ASTNode *fallbackAst) {
  ASTNode *source = module.originAst ? module.originAst : fallbackAst;
  if (!source)
    return nullptr;
  return std::make_unique<CodeGen>(source);
}

}  // namespace sys::hir
