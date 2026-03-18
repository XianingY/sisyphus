# HIR Layer (Structured Frontend IR)

This directory hosts a lightweight high-level IR layer inspired by MLIR/ClangIR staging:

- `HIROps.*`: dialect-style op kinds + traits (`Pure`, `MemoryEffect`, `BranchLike`, `LoopLike`)
- `HIRBuilder.*`: AST -> HIR construction
- `HIRVerifier.*`: structural/type/symbol checks
- `HIRCanonicalize.*`: lightweight canonicalization (const fold + dead branch cleanup)
- `HIRLowering.*`: legacy bridge (kept for migration compatibility)

Current rollout strategy:

1. Default compilation path uses dialect frontend:
   `AST -> HIR -> CFG -> legacy ModuleOp adapter`.
2. `--use-legacy-codegen` switches back to legacy `AST -> CodeGen` path.
3. Backend and existing O1/O2 pipelines remain unchanged.
