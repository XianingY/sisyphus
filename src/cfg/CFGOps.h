#ifndef CFG_OPS_H
#define CFG_OPS_H

#include <ostream>
#include <string>
#include <vector>

#include "../hir/HIROps.h"
#include "../parse/ASTNode.h"

namespace sys::cfg {

enum class OpKind {
  Nop,
  Call,
  Load,
  Store,
  Arith,
  Cmp,
  Phi,
  Ret,
  Br,
  CondBr,
};

struct Inst {
  OpKind kind = OpKind::Nop;
  hir::TypeKind type = hir::TypeKind::Unknown;
  std::string result;
  std::string symbol;
  std::vector<std::string> args;
  std::vector<int> targets;
  std::vector<int> phiPreds;
};

struct Block {
  std::string name;
  std::vector<Inst> insts;
};

struct Func {
  std::string name;
  int entry = 0;
  std::vector<Block> blocks;
};

struct Module {
  ASTNode *originAst = nullptr;
  std::vector<Func> funcs;
};

bool isTerminator(OpKind kind);
const char *kindName(OpKind kind);
void dump(const Module &module, std::ostream &os);

}  // namespace sys::cfg

#endif
