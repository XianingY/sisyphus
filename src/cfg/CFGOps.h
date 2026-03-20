#ifndef CFG_OPS_H
#define CFG_OPS_H

#include <ostream>
#include <cstddef>
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

enum class MemoryBaseKind {
  Unknown,
  Global,
  Local,
  Param,
};

struct SymbolInfo {
  std::string name;
  hir::TypeKind type = hir::TypeKind::Unknown;
  hir::TypeKind elementType = hir::TypeKind::Unknown;
  std::vector<int> dims;
  std::vector<size_t> strideBytes;
  MemoryBaseKind baseKind = MemoryBaseKind::Unknown;
  bool isGlobal = false;
  bool isParam = false;
  bool isMutable = true;
  size_t elemSize = 4;
  size_t storageSize = 4;
  bool hasIntInit = false;
  long long intInit = 0;
  bool hasFloatInit = false;
  double floatInit = 0.0;
  std::vector<int> intArrayInit;
  std::vector<float> floatArrayInit;
};

struct Inst {
  OpKind kind = OpKind::Nop;
  hir::TypeKind type = hir::TypeKind::Unknown;
  hir::TypeKind elementType = hir::TypeKind::Unknown;
  std::string result;
  std::string symbol;
  std::vector<std::string> args;
  size_t memSize = 0;
  std::vector<size_t> strideBytes;
  MemoryBaseKind baseKind = MemoryBaseKind::Unknown;
  int accessRank = 0;
  bool producesAddress = false;
  hir::TypeKind calleeRetType = hir::TypeKind::Unknown;
  std::vector<hir::TypeKind> calleeArgTypes;
  std::vector<int> targets;
  std::vector<int> phiPreds;
};

struct Block {
  std::string name;
  std::vector<Inst> insts;
};

struct Func {
  std::string name;
  hir::TypeKind returnType = hir::TypeKind::Unknown;
  std::vector<SymbolInfo> params;
  std::vector<SymbolInfo> locals;
  int entry = 0;
  std::vector<Block> blocks;
};

struct Module {
  ASTNode *originAst = nullptr;
  std::vector<SymbolInfo> globals;
  std::vector<Func> funcs;
};

bool isTerminator(OpKind kind);
const char *kindName(OpKind kind);
void dump(const Module &module, std::ostream &os);

}  // namespace sys::cfg

#endif
