#ifndef HIR_BUILDER_H
#define HIR_BUILDER_H

#include "HIROps.h"

namespace sys::hir {

class Builder {
public:
  Module build(ASTNode *node);

private:
  std::unique_ptr<Op> buildNode(ASTNode *node);
  std::unique_ptr<Op> buildBlockLike(ASTNode *node);
};

}  // namespace sys::hir

#endif
