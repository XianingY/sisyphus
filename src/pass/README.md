# Pass Mapping

Optimization and lowering passes are currently implemented in:

- `src/pre-opt`: structured control-flow optimization passes
- `src/opt`: flattened CFG/SSA optimization passes
- `src/rv` and `src/arm`: backend-specific lowering/cleanup passes

This directory is reserved for a unified pass registry API.
