#include "PipelineProfiles.h"

#include <sstream>

#include "../opt/Passes.h"
#include "../opt/LoopPasses.h"
#include "../opt/CleanupPasses.h"
#include "../opt/LowerPasses.h"
#include "../opt/SMTPasses.h"
#include "../opt/Analysis.h"
#include "../pre-opt/PrePasses.h"
#include "../pre-opt/PreLoopPasses.h"
#include "../pre-opt/PreAnalysis.h"
#include "../arm/ArmPasses.h"
#include "../arm/ArmLoopPasses.h"
#include "../rv/RvPasses.h"
#include "../rv/RvDupPasses.h"

namespace sys::pipeline {

namespace {

void appendArmBackend(sys::PassManager &pm, const sys::Options &opts) {
  pm.addPass<sys::arm::Lower>();
  pm.addPass<sys::arm::StrengthReduct>();
  pm.addPass<sys::arm::InstCombine>();
  pm.addPass<sys::arm::ArmDCE>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::arm::PostIncr>();
  pm.addPass<sys::arm::ArmDCE>();
  pm.addPass<sys::arm::RegAlloc>();
  pm.addPass<sys::arm::LateLegalize>();
  pm.addPass<sys::arm::Dump>(opts.outputFile);
}

void appendRvBackend(sys::PassManager &pm, const sys::Options &opts) {
  pm.addPass<sys::rv::Lower>();
  pm.addPass<sys::rv::StrengthReduct>();
  pm.addPass<sys::rv::InstCombine>();
  pm.addPass<sys::rv::RvDCE>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::rv::RegAlloc>();
  pm.addPass<sys::rv::Dump>(opts.outputFile);
}

void appendCoreO0(sys::PassManager &pm) {
  pm.addPass<sys::MoveAlloca>();

  pm.addPass<sys::EarlyConstFold>(/*beforePureness=*/ true);
  pm.addPass<sys::Pureness>();
  pm.addPass<sys::EarlyConstFold>(/*beforePureness=*/ false);
  pm.addPass<sys::RaiseToFor>();
  pm.addPass<sys::DCE>(/*elimBlocks=*/ false);
  pm.addPass<sys::Lower>();

  pm.addPass<sys::FlattenCFG>();
  pm.addPass<sys::Mem2Reg>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::SimplifyCFG>();
  pm.addPass<sys::Select>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::InstSchedule>();
}

void appendCoreO1(sys::PassManager &pm, const sys::Options &opts, bool aggressive) {
  const bool enableO2Experimental = aggressive && !opts.disableO2Experimental;

  pm.addPass<sys::MoveAlloca>();

  pm.addPass<sys::AtMostOnce>();
  pm.addPass<sys::Localize>(/*beforeFlattenCFG=*/ true);
  pm.addPass<sys::EarlyConstFold>(/*beforePureness=*/ true);
  pm.addPass<sys::Pureness>();
  pm.addPass<sys::EarlyConstFold>(/*beforePureness=*/ false);
  pm.addPass<sys::TCO>();
  pm.addPass<sys::Remerge>();
  pm.addPass<sys::RaiseToFor>();
  pm.addPass<sys::DCE>(/*elimBlocks=*/ false);
  pm.addPass<sys::EarlyInline>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::View>();
  pm.addPass<sys::LoopDCE>();
  pm.addPass<sys::TidyMemory>();
  if (aggressive) {
    pm.addPass<sys::Fusion>();
    pm.addPass<sys::Unswitch>();
  }
  pm.addPass<sys::DCE>(/*elimBlocks=*/ false);
  pm.addPass<sys::ColumnMajor>();
  pm.addPass<sys::Parallelizable>();
  pm.addPass<sys::LoopDCE>();
  pm.addPass<sys::Lower>();

  pm.addPass<sys::FlattenCFG>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::Inline>(/*inlineThreshold=*/ opts.inlineThreshold);
  pm.addPass<sys::DCE>();
  pm.addPass<sys::Localize>(/*beforeFlattenCFG=*/ false);
  pm.addPass<sys::Globalize>();

  pm.addPass<sys::Mem2Reg>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::DAE>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::DSE>();
  pm.addPass<sys::DLE>();
  pm.addPass<sys::GVN>();
  if (aggressive)
    pm.addPass<sys::Reassociate>();

  pm.addPass<sys::CanonicalizeLoop>(/*lcssa=*/ true);
  if (!opts.disableLoopRotate)
    pm.addPass<sys::LoopRotate>();
  pm.addPass<sys::CanonicalizeLoop>(/*lcssa=*/ false);
  pm.addPass<sys::LICM>();
  if (!opts.disableConstUnroll)
    pm.addPass<sys::ConstLoopUnroll>();
  pm.addPass<sys::SCEV>();
  pm.addPass<sys::AggressiveDCE>();
  if (opts.arm)
    pm.addPass<sys::Vectorize>();
  pm.addPass<sys::GVN>();

  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::SimplifyCFG>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::DAE>();
  pm.addPass<sys::DSE>();
  pm.addPass<sys::DLE>();
  pm.addPass<sys::Select>();
  if (aggressive) {
    pm.addPass<sys::Range>();
    pm.addPass<sys::RangeAwareFold>();
    pm.addPass<sys::Splice>();
  }
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::GCM>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::AggressiveDCE>();

  pm.addPass<sys::LateInline>(/*threshold=*/ opts.lateInlineThreshold);
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::DSE>();
  pm.addPass<sys::DLE>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::InlineStore>();
  if (enableO2Experimental)
    pm.addPass<sys::Cached>();
  if (enableO2Experimental)
    pm.addPass<sys::SynthConstArray>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::GCM>();
  pm.addPass<sys::GVN>();

  for (int i = 0; i < 3; i++) {
    pm.addPass<sys::CanonicalizeLoop>(/*lcssa=*/ true);
    pm.addPass<sys::LICM>();
    pm.addPass<sys::SCEV>();
    pm.addPass<sys::RemoveEmptyLoop>();
    pm.addPass<sys::GVN>();
    pm.addPass<sys::RegularFold>();
  }
  if (aggressive) {
    pm.addPass<sys::CanonicalizeLoop>(/*lcssa=*/ true);
    pm.addPass<sys::LICM>();
    pm.addPass<sys::SCEV>();
    pm.addPass<sys::GVN>();
    pm.addPass<sys::RegularFold>();
  }

  if (aggressive) {
    pm.addPass<sys::CanonicalizeLoop>(/*lcssa=*/ true);
    pm.addPass<sys::LICM>();
    pm.addPass<sys::SCEV>();
    pm.addPass<sys::GVN>();
    pm.addPass<sys::RegularFold>();
    pm.addPass<sys::DCE>();
  }
  pm.addPass<sys::AggressiveDCE>();
  pm.addPass<sys::SimplifyCFG>();
  pm.addPass<sys::InstSchedule>();
}

const char *coreProfileName(CoreProfile profile) {
  switch (profile) {
  case CoreProfile::O0:
    return "O0";
  case CoreProfile::O1:
    return "O1";
  case CoreProfile::O2:
    return "O2";
  }
  return "unknown";
}

}  // namespace

PipelinePlan selectPlan(const Options &opts) {
  PipelinePlan plan;
  if (opts.o2)
    plan.coreProfile = CoreProfile::O2;
  else if (opts.o1)
    plan.coreProfile = CoreProfile::O1;
  else
    plan.coreProfile = CoreProfile::O0;
  plan.aggressive = opts.o2;
  plan.enableO2Experimental = opts.o2 && !opts.disableO2Experimental;
  plan.useArmBackend = opts.arm;
  plan.useRvBackend = opts.rv;
  return plan;
}

PipelinePlan configurePipeline(PassManager &pm, const Options &opts) {
  auto plan = selectPlan(opts);
  switch (plan.coreProfile) {
  case CoreProfile::O0:
    appendCoreO0(pm);
    break;
  case CoreProfile::O1:
  case CoreProfile::O2:
    appendCoreO1(pm, opts, plan.aggressive);
    break;
  }

  if (plan.useArmBackend)
    appendArmBackend(pm, opts);
  if (plan.useRvBackend)
    appendRvBackend(pm, opts);
  return plan;
}

std::string formatPlan(const PipelinePlan &plan) {
  std::ostringstream oss;
  oss << "core=" << coreProfileName(plan.coreProfile)
      << ", aggressive=" << (plan.aggressive ? "1" : "0")
      << ", o2_experimental=" << (plan.enableO2Experimental ? "1" : "0")
      << ", backend=[";
  bool first = true;
  if (plan.useArmBackend) {
    oss << "arm";
    first = false;
  }
  if (plan.useRvBackend) {
    if (!first)
      oss << ",";
    oss << "riscv";
  }
  oss << "]";
  return oss.str();
}

}  // namespace sys::pipeline
