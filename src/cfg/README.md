# CFG Dialect Layer

This directory provides an explicit control-flow dialect between HIR and
legacy `ModuleOp`:

- `CFGOps.*`: block/inst model (`br`, `cond_br`, `phi`, `load`, `store`, ...)
- `HIRToCFG.*`: structured HIR lowering into explicit CFG blocks
- `CFGVerifier.*`: block terminator, pred/succ, phi incoming, unreachable checks
- `CFGLegality.*`: legality set checks for HIR/CFG and conversion boundary
- `CFGToLegacy.*`: compatibility adapter into existing `ModuleOp` pipeline

The dialect pipeline is:

`HIR -> verify -> canonicalize -> verify -> CFG -> verify -> legacy ModuleOp`
