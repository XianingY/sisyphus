#ifndef BACKEND_SHARED_REG_ALLOC_HOTNESS_H
#define BACKEND_SHARED_REG_ALLOC_HOTNESS_H

#include "../../codegen/CodeGen.h"
#include "../../codegen/Attrs.h"

#include <unordered_map>

namespace sys::backend::shared {

template <class IsCallLike>
std::unordered_map<BasicBlock*, int> computeBlockHotness(Region *region,
                                                         IsCallLike isCallLike,
                                                         int backEdgeMultiplier = 8,
                                                         int callLikeMultiplier = 2) {
  std::unordered_map<BasicBlock*, int> bbIndex;
  std::unordered_map<BasicBlock*, int> bbWeight;
  int idx = 0;
  for (auto bb : region->getBlocks())
    bbIndex[bb] = idx++;

  for (auto bb : region->getBlocks()) {
    int weight = 1;
    if (bb->getOpCount() == 0) {
      bbWeight[bb] = weight;
      continue;
    }

    auto term = bb->getLastOp();
    bool hasBackEdge = false;
    if (auto target = term->find<TargetAttr>()) {
      if (bbIndex.count(target->bb))
        hasBackEdge = hasBackEdge || (bbIndex[target->bb] <= bbIndex[bb]);
    }
    if (auto ifnot = term->find<ElseAttr>()) {
      if (bbIndex.count(ifnot->bb))
        hasBackEdge = hasBackEdge || (bbIndex[ifnot->bb] <= bbIndex[bb]);
    }
    if (hasBackEdge)
      weight *= backEdgeMultiplier;

    for (auto op : bb->getOps()) {
      if (!isCallLike(op))
        continue;
      weight *= callLikeMultiplier;
      break;
    }
    bbWeight[bb] = weight;
  }

  return bbWeight;
}

}  // namespace sys::backend::shared

#endif
