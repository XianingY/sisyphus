#ifndef CFG_LEGALITY_H
#define CFG_LEGALITY_H

#include "CFGOps.h"

#include "../hir/HIROps.h"

#include <string>
#include <vector>

namespace sys::cfg {

bool verifyHIRLegalSet(const hir::Module &module, std::vector<std::string> &errors);
bool verifyCFGLegalSet(const Module &module, std::vector<std::string> &errors);
bool verifyHIRToCFGConversion(const hir::Module &hirModule, const Module &cfgModule, std::vector<std::string> &errors);

}  // namespace sys::cfg

#endif
