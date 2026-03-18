#ifndef HIR_TO_CFG_H
#define HIR_TO_CFG_H

#include "CFGOps.h"

#include <string>
#include <vector>

namespace sys::cfg {

Module lowerFromHIR(const hir::Module &hirModule, std::vector<std::string> &errors);

}  // namespace sys::cfg

#endif
