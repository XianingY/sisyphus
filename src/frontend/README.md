# Frontend Mapping

## Current State
- Canonical entry for new integration: `src/frontend/FrontendFacade.h`
- Current implementation lives in:
  - `src/parse`: lexer, parser, type system
  - `src/codegen`: AST-to-IR lowering

## Boundary Rule
- New top-level wiring should include frontend facade instead of raw parse/codegen paths.

## Next Steps
- Introduce frontend-only tests and diagnostics under `src/frontend` namespace.
- Migrate parse/codegen internals incrementally.
