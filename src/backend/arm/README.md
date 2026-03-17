# ARM Backend Mapping

## Current State
- Canonical entry for new integration: `src/backend/arm/BackendPasses.h`
- ARM64 implementation still lives in `src/arm`

## Boundary Rule
- New cross-module includes should reference `src/backend/arm/*` first.
- `src/arm/*` remains implementation detail during migration.

## Next Steps
- Move pass declarations to backend namespace wrappers first.
- Migrate implementation files incrementally (no bulk move).
