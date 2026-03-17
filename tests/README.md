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
scripts/suite-sync.sh --update
scripts/suite-index.sh
scripts/gen-reference-out.sh compiler-dev
scripts/eval-runtime.sh open-functional riscv O1
# allow perf/* timeout as soft-fail (functional remains hard gate)
RUNTIME_SOFT_PERF=1 scripts/eval-runtime.sh compiler-dev arm O2
# set a larger timeout budget for perf/* only (functional keeps base timeout)
RUNTIME_SOFT_PERF=1 RUNTIME_PERF_TIMEOUT_SEC=20 scripts/eval-runtime.sh compiler-dev arm O2
scripts/eval-hotspots.sh arm O2 20
scripts/eval-vs-biframe.sh open-functional riscv O1
scripts/runtime-summary.sh
scripts/runtime-gate.sh
# aggressive O2 tuning gate (O1 vs O2, defaults to perf timeout 20s)
scripts/eval-o2-aggressive.sh compiler-dev riscv
scripts/eval-o2-aggressive.sh compiler-dev arm 20
# emergency stop for O2-only experimental passes
scripts/regression.sh /path/to/cases riscv O2 --disable-o2-experimental
```
