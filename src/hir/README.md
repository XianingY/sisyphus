# HIR Layer (Structured Frontend IR)

This directory hosts a lightweight high-level IR layer inspired by MLIR/ClangIR staging:

- `HIROps.*`: dialect-style op kinds + traits (`Pure`, `MemoryEffect`, `BranchLike`, `LoopLike`)
- `HIRBuilder.*`: AST -> HIR construction
- `HIRVerifier.*`: structural/type/symbol checks
- `HIRCanonicalize.*`: lightweight canonicalization (const fold + dead branch cleanup)
- `HIRLowering.*`: bridge from HIR pipeline back to existing legacy IR codegen

Current rollout strategy:

1. Default compilation path remains legacy (`AST -> CodeGen -> existing passes`).
2. `--enable-hir-pipeline` enables staged flow:
   `AST -> HIR build -> HIR verify -> HIR canonicalize -> HIR verify -> legacy lowering`.
3. Backend and existing O1/O2 pipelines remain unchanged.
