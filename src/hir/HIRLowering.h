#ifndef HIR_LOWERING_H
#define HIR_LOWERING_H

#include "HIROps.h"

#include <memory>

#include "../codegen/CodeGen.h"

namespace sys::hir {

std::unique_ptr<CodeGen> lowerToLegacyIR(const Module &module, ASTNode *fallbackAst);

}  // namespace sys::hir

#endif
