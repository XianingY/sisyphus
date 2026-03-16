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
