#ifndef CFG_TO_LEGACY_H
#define CFG_TO_LEGACY_H

#include "CFGOps.h"

#include "../codegen/CodeGen.h"

#include <memory>
#include <string>
#include <vector>

namespace sys::cfg {

bool verifyCFGToLegacyLegality(const Module &cfgModule, ModuleOp *legacyModule, std::vector<std::string> &errors);
std::unique_ptr<CodeGen> lowerToLegacyIR(const Module &cfgModule, ASTNode *fallbackAst, std::vector<std::string> &errors);

}  // namespace sys::cfg

#endif
