#include "Analysis.h"

#include <unordered_map>

using namespace sys;

namespace {

bool isIntegralValue(Op *op) {
  auto ty = op->getResultType();
  return ty == Value::i32 || ty == Value::i64;
}

bool isConstInt(Op *op, int v) {
  return isa<IntOp>(op) && V(op) == v;
}

class UnionFind {
  std::unordered_map<Op*, Op*> parent;
  std::unordered_map<Op*, int> rank;

public:
  void add(Op *op) {
    if (!op || parent.count(op))
      return;
    parent[op] = op;
    rank[op] = 0;
  }

  bool has(Op *op) const {
    return parent.count(op);
  }

  Op *find(Op *op) {
    auto it = parent.find(op);
    if (it == parent.end())
      return op;
    if (it->second == op)
      return op;
    it->second = find(it->second);
    return it->second;
  }

  void unite(Op *a, Op *b) {
    if (!a || !b || !has(a) || !has(b))
      return;
    a = find(a);
    b = find(b);
    if (a == b)
      return;
    if (rank[a] < rank[b])
      std::swap(a, b);
    parent[b] = a;
    if (rank[a] == rank[b])
      rank[a]++;
  }

  const auto &all() const { return parent; }
};

void inferIdentityUnions(Op *op, UnionFind &uf) {
  auto unifyWith = [&](Op *x) {
    if (isIntegralValue(op) && isIntegralValue(x))
      uf.unite(op, x);
  };

  if (isa<PhiOp>(op)) {
    if (op->getOperandCount() == 0)
      return;
    Op *first = op->DEF(0);
    if (!first || !uf.has(first))
      return;
    bool allSame = true;
    for (int i = 1; i < op->getOperandCount(); i++) {
      Op *d = op->DEF(i);
      if (!d || !uf.has(d) || uf.find(d) != uf.find(first)) {
        allSame = false;
        break;
      }
    }
    if (allSame)
      unifyWith(first);
    return;
  }

  if (isa<SelectOp>(op) && op->getOperandCount() == 3) {
    Op *cond = op->DEF(0);
    Op *t = op->DEF(1);
    Op *f = op->DEF(2);
    if (isConstInt(cond, 0))
      unifyWith(f);
    else if (isa<IntOp>(cond))
      unifyWith(t);
    return;
  }

  if (isa<AddIOp>(op) && op->getOperandCount() == 2) {
    Op *a = op->DEF(0), *b = op->DEF(1);
    if (isConstInt(a, 0))
      unifyWith(b);
    else if (isConstInt(b, 0))
      unifyWith(a);
    return;
  }

  if (isa<SubIOp>(op) && op->getOperandCount() == 2) {
    Op *a = op->DEF(0), *b = op->DEF(1);
    if (isConstInt(b, 0))
      unifyWith(a);
    return;
  }

  if (isa<MulIOp>(op) && op->getOperandCount() == 2) {
    Op *a = op->DEF(0), *b = op->DEF(1);
    if (isConstInt(a, 1))
      unifyWith(b);
    else if (isConstInt(b, 1))
      unifyWith(a);
    return;
  }

  if (isa<DivIOp>(op) && op->getOperandCount() == 2) {
    Op *a = op->DEF(0), *b = op->DEF(1);
    if (isConstInt(b, 1))
      unifyWith(a);
    return;
  }

  if (isa<AndIOp>(op) && op->getOperandCount() == 2) {
    Op *a = op->DEF(0), *b = op->DEF(1);
    if (isConstInt(a, -1))
      unifyWith(b);
    else if (isConstInt(b, -1))
      unifyWith(a);
    return;
  }

  if ((isa<OrIOp>(op) || isa<XorIOp>(op)) && op->getOperandCount() == 2) {
    Op *a = op->DEF(0), *b = op->DEF(1);
    if (isConstInt(a, 0))
      unifyWith(b);
    else if (isConstInt(b, 0))
      unifyWith(a);
    return;
  }

  if ((isa<LShiftOp>(op) || isa<RShiftOp>(op)) && op->getOperandCount() == 2) {
    Op *a = op->DEF(0), *b = op->DEF(1);
    if (isConstInt(b, 0))
      unifyWith(a);
  }
}

}

void sys::removeEqClass(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps())
      op->remove<EqClassAttr>();
  }
}

void EqClass::run() {
  classes = 0;
  auto funcs = collectFuncs();

  for (auto func : funcs) {
    auto region = func->getRegion();
    removeEqClass(region);

    UnionFind uf;
    std::unordered_map<int, Op*> constRep;

    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps()) {
        if (!isIntegralValue(op))
          continue;
        uf.add(op);
        if (isa<IntOp>(op)) {
          auto it = constRep.find(V(op));
          if (it == constRep.end())
            constRep[V(op)] = op;
          else
            uf.unite(op, it->second);
        }
      }
    }

    // A few rounds to settle phi/select identities after simple unions.
    for (int i = 0; i < 3; i++) {
      bool changed = false;
      for (auto bb : region->getBlocks()) {
        for (auto op : bb->getOps()) {
          if (!isIntegralValue(op) || !uf.has(op))
            continue;
          Op *before = uf.find(op);
          inferIdentityUnions(op, uf);
          changed |= uf.find(op) != before;
        }
      }
      if (!changed)
        break;
    }

    std::unordered_map<Op*, int> idMap;
    for (auto [op, _] : uf.all()) {
      Op *root = uf.find(op);
      if (!idMap.count(root))
        idMap[root] = ++classes;
      op->add<EqClassAttr>(idMap[root]);
    }
  }
}

