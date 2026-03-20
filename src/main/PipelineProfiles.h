#ifndef PIPELINE_PROFILES_H
#define PIPELINE_PROFILES_H

#include <cstddef>
#include <string>

#include "Options.h"
#include "../opt/PassManager.h"

namespace sys::pipeline {

enum class CoreProfile {
  O0,
  O1,
  O2,
};

enum class FrontendProfile {
  Legacy,
  Dialect,
};

struct PipelineMetrics {
  size_t moduleOpCount = 0;
  size_t blockCount = 0;
  size_t cfgEdgeCount = 0;
  size_t phiCount = 0;
  size_t callLikeCount = 0;
  size_t getArgCount = 0;
  int maxGetArgArity = 0;
  int maxLoopDepth = 0;
};

struct PipelinePlan {
  FrontendProfile frontendProfile;
  CoreProfile coreProfile;
  bool aggressive;
  bool enableO2Experimental;
  bool enableO2Heavy;
  int o2LoopRounds;
  bool largeModuleMode;
  bool hugeModuleMode;
  bool backendFastMode;
  bool armTimeoutSafeMode;
  int armInstCombineRounds;
  int armPeepholeRounds;
  int armRegAllocCallPenalty;
  int armRegAllocLoopBoost;
  int armRegAllocPreferBudget;
  PipelineMetrics metrics;
  bool useArmBackend;
  bool useRvBackend;
};

PipelinePlan selectPlan(const Options &opts, PipelineMetrics metrics = {});
PipelinePlan configurePipeline(PassManager &pm, const Options &opts, PipelineMetrics metrics = {});
std::string formatPlan(const PipelinePlan &plan);

}

#endif
