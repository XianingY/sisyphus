#ifndef HIR_VERIFIER_H
#define HIR_VERIFIER_H

#include "HIROps.h"

#include <string>
#include <vector>

namespace sys::hir {

bool verify(const Module &module, std::vector<std::string> &errors);

}  // namespace sys::hir

#endif
