#ifndef CFG_VERIFIER_H
#define CFG_VERIFIER_H

#include "CFGOps.h"

#include <string>
#include <vector>

namespace sys::cfg {

bool verify(const Module &module, std::vector<std::string> &errors);

}  // namespace sys::cfg

#endif
