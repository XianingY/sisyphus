#ifndef PIPELINE_PROFILES_H
#define PIPELINE_PROFILES_H

#include <string>

#include "Options.h"
#include "../opt/PassManager.h"

namespace sys::pipeline {

enum class CoreProfile {
  O0,
  O1,
  O2,
};

struct PipelinePlan {
  CoreProfile coreProfile;
  bool aggressive;
  bool enableO2Experimental;
  bool useArmBackend;
  bool useRvBackend;
};

PipelinePlan selectPlan(const Options &opts);
PipelinePlan configurePipeline(PassManager &pm, const Options &opts);
std::string formatPlan(const PipelinePlan &plan);

}

#endif
