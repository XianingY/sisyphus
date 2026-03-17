# RISC-V Backend Mapping

## Current State
- Canonical entry for new integration: `src/backend/riscv/BackendPasses.h`
- RV64 implementation still lives in `src/rv`

## Boundary Rule
- New cross-module includes should reference `src/backend/riscv/*` first.
- `src/rv/*` remains implementation detail during migration.

## Next Steps
- Keep dual-target wiring under backend adapters.
- Migrate RV implementation files in small batches.
