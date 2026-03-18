#ifndef CFG_TO_LEGACY_H
#define CFG_TO_LEGACY_H

#include "CFGOps.h"
#include "../codegen/Ops.h"

#include <memory>
#include <string>
#include <vector>

namespace sys::cfg {

bool verifyCFGToLegacyLegality(const Module &cfgModule, ModuleOp *legacyModule, std::vector<std::string> &errors);
std::unique_ptr<ModuleOp> lowerToLegacyIR(const Module &cfgModule, std::vector<std::string> &errors);

}  // namespace sys::cfg

#endif
