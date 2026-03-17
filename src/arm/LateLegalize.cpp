#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

void LateLegalize::run() {
  Builder builder;
  auto fitsAddImm12 = [](int imm) { return imm >= -4095 && imm <= 4095; };
  
  // ARM does not support `add x0, xzr, 1`.
  runRewriter([&](AddXIOp *op) {
    if (RS(op) == Reg::xzr)
      builder.replace<MovIOp>(op, { RDC(RD(op)), new IntAttr(V(op)) });

    int imm = V(op);
    if (!fitsAddImm12(imm)) {
      builder.setBeforeOp(op);
      Reg rd = RD(op);
      Reg rs = RS(op);
      int remain = imm;
      int step = remain;
      if (step > 4095)
        step = 4095;
      if (step < -4095)
        step = -4095;
      builder.create<AddXIOp>({ RDC(rd), RSC(rs), new IntAttr(step) });
      remain -= step;
      while (remain != 0) {
        step = remain;
        if (step > 4095)
          step = 4095;
        if (step < -4095)
          step = -4095;
        builder.create<AddXIOp>({ RDC(rd), RSC(rd), new IntAttr(step) });
        remain -= step;
      }
      op->erase();
      return true;
    }
    
    return false;
  });

  runRewriter([&](AddWIOp *op) {
    if (RS(op) == Reg::xzr)
      builder.replace<MovIOp>(op, { RDC(RD(op)), new IntAttr(V(op)) });
    
    return false;
  });

  // Use `mov` and `movk` for an out-of-range `mov`.
  runRewriter([&](MovIOp *op) {
    int v = V(op);
    if (v >= 65536) {
      builder.setBeforeOp(op);
      builder.create<MovIOp>({ RDC(RD(op)), new IntAttr(v & 0xffff) });
      builder.replace<MovkOp>(op, { RDC(RD(op)), new IntAttr(((unsigned) v) >> 16), new LslAttr(16) });
    }
    if (v < -65536) {
      unsigned u = v;

      builder.setBeforeOp(op);
      builder.create<MovnOp>({ RDC(RD(op)), new IntAttr((uint16_t)(~(uint16_t)(u & 0xffff))) });
      builder.replace<MovkOp>(op, { RDC(RD(op)), new IntAttr(u >> 16), new LslAttr(16) });
    }
    return false;
  });
}
