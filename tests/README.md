# Tests

- `tests/smoke`: minimal dual-target compile regression cases
- `tests/regression/frontend`: frontend correctness and CLI-guard regressions
- Execute with:

```bash
scripts/run_smoke.sh
scripts/run_frontend_regressions.sh
scripts/run_arm_o2_fft_regressions.sh
```

For batch compilation against a local SysY case directory:

```bash
scripts/regression.sh /path/to/cases riscv O1
scripts/regression.sh /path/to/cases arm O1
# with extra compiler args
scripts/regression.sh /path/to/cases riscv O1 --inline-threshold=160 --disable-loop-rotate
```

For semantic consistency check (`--compare`) and assembly-size proxy:

```bash
scripts/compare.sh /path/to/cases riscv O1
scripts/compare.sh /path/to/cases arm O1
# include perf/* cases explicitly
COMPARE_INCLUDE_PERF=1 scripts/compare.sh /path/to/cases riscv O1
# per-case timeout and interpreter step budget
COMPARE_TIMEOUT_SEC=60 SISY_EXEC_STEP_LIMIT=80000000 scripts/compare.sh /path/to/cases riscv O1
scripts/asm-delta.sh /path/to/cases riscv
scripts/eval-o1-matrix.sh /path/to/cases
```

For public suite sync/index and runtime evaluation:

```bash
scripts/suite-sync.sh --update --src-root /home/wslootie/github/cpe/compiler2025
scripts/suite-index.sh
# hard functional gate
scripts/eval-runtime.sh official-functional riscv O1
# soft perf gate with 20s timeout window
RUNTIME_SOFT_PERF=1 RUNTIME_PERF_TIMEOUT_SEC=20 scripts/eval-runtime.sh official-arm-perf arm O2
RUNTIME_SOFT_PERF=1 RUNTIME_PERF_TIMEOUT_SEC=20 scripts/eval-runtime.sh official-riscv-perf riscv O1
scripts/eval-hotspots.sh arm O2 20
scripts/eval-vs-biframe.sh official-functional riscv O1
scripts/runtime-summary.sh
scripts/runtime-gate.sh
# aggressive O2 tuning gate (O1 vs O2, defaults to perf timeout 20s)
scripts/eval-o2-aggressive.sh official-riscv-perf riscv
scripts/eval-o2-aggressive.sh official-arm-perf arm 20
# emergency stop for O2-only experimental passes
scripts/regression.sh /path/to/cases riscv O2 --disable-o2-experimental
```
