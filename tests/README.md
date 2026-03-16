# Tests

- `tests/smoke`: minimal dual-target compile regression cases
- Execute with:

```bash
scripts/run_smoke.sh
```

For batch compilation against a local SysY case directory:

```bash
scripts/regression.sh /path/to/cases riscv O1
scripts/regression.sh /path/to/cases arm O1
```

For semantic consistency check (`--compare`) and assembly-size proxy:

```bash
scripts/compare.sh /path/to/cases riscv O1
scripts/compare.sh /path/to/cases arm O1
scripts/asm-delta.sh /path/to/cases riscv
```
