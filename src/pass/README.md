# Pass Mapping

## Current State
- Canonical pass entry: `src/pass/PassRegistry.h`
- Current implementation lives in:
  - `src/pre-opt`: structured control-flow passes
  - `src/opt`: flattened CFG/SSA passes
  - `src/rv` and `src/arm`: backend lowering/cleanup passes

## Boundary Rule
- New wiring should use pass registry + pipeline profile APIs.
- Direct includes to individual pass directories are allowed only in implementation files.

## Next Steps
- Expand pass registry to expose named profile presets and profile dumps.
- Keep pass ordering centralized under pipeline profile implementation.
