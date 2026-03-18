#include "CFGOps.h"

namespace sys::cfg {

bool isTerminator(OpKind kind) {
  return kind == OpKind::Br || kind == OpKind::CondBr || kind == OpKind::Ret;
}

const char *kindName(OpKind kind) {
  switch (kind) {
  case OpKind::Nop:
    return "nop";
  case OpKind::Call:
    return "call";
  case OpKind::Load:
    return "load";
  case OpKind::Store:
    return "store";
  case OpKind::Arith:
    return "arith";
  case OpKind::Cmp:
    return "cmp";
  case OpKind::Phi:
    return "phi";
  case OpKind::Ret:
    return "ret";
  case OpKind::Br:
    return "br";
  case OpKind::CondBr:
    return "cond_br";
  }
  return "unknown";
}

void dump(const Module &module, std::ostream &os) {
  os << "cfg.module\n";
  for (const auto &func : module.funcs) {
    os << "  cfg.func @" << func.name << " entry=" << func.entry << "\n";
    for (size_t bid = 0; bid < func.blocks.size(); bid++) {
      const auto &bb = func.blocks[bid];
      os << "    ^bb" << bid << " (" << bb.name << ")\n";
      for (const auto &inst : bb.insts) {
        os << "      ";
        if (!inst.result.empty())
          os << inst.result << " = ";
        os << kindName(inst.kind);
        if (!inst.symbol.empty())
          os << " \"" << inst.symbol << "\"";
        if (!inst.args.empty()) {
          os << " [";
          for (size_t i = 0; i < inst.args.size(); i++) {
            if (i)
              os << ", ";
            os << inst.args[i];
          }
          os << "]";
        }
        if (!inst.targets.empty()) {
          os << " -> [";
          for (size_t i = 0; i < inst.targets.size(); i++) {
            if (i)
              os << ", ";
            os << "bb" << inst.targets[i];
          }
          os << "]";
        }
        if (!inst.phiPreds.empty()) {
          os << " preds=[";
          for (size_t i = 0; i < inst.phiPreds.size(); i++) {
            if (i)
              os << ", ";
            os << "bb" << inst.phiPreds[i];
          }
          os << "]";
        }
        os << "\n";
      }
    }
  }
}

}  // namespace sys::cfg
