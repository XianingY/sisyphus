# Sisyphus

Sisyphus is a SysY compiler project for the 2025 compiler contest track.

## Features

- Unified frontend + IR pipeline
- Dual backends:
  - RISC-V (`rv64gc`)
  - ARM64 (`AArch64`, ARMv8-A)
- Competition-compatible CLI:
  - `compiler testcase.sy -S -o testcase.s`
  - `compiler testcase.sy -S -o testcase.s -O1`
  - `compiler testcase.sy -S -o testcase.s -O2`
- Debug and tuning flags:
  - `--target=riscv|arm`
  - `--emit-ir`
  - `--verify-ir`
  - `--dump-pass-timing`
  - `--enable-experimental` (opt-in for experimental O1/O2 passes; off by default)
  - `--inline-threshold=<N>`
  - `--late-inline-threshold=<N>`
  - `--disable-loop-rotate`
  - `--enable-loop-rotate`
  - `--disable-const-unroll`
  - Defaults: `-O1 => inline/late=200`, `-O2 => inline/late=256` (unless explicitly overridden), and loop-rotate off by default for O1/O2

## Build

```bash
scripts/build.sh
```

Default target can be changed at configure time:

```bash
DEFAULT_TARGET=arm scripts/build.sh
```

or with raw CMake:

```bash
cmake -S . -B build -DDEFAULT_TARGET=arm
cmake --build build -j
```

## Usage

```bash
# RISC-V (default)
./build/compiler tests/smoke/basic.sy -S -o basic.rv.s -O1

# ARM
./build/compiler tests/smoke/basic.sy -S -o basic.arm.s -O1 --target=arm

# Aggressive profile
./build/compiler tests/smoke/basic.sy -S -o basic.rv.o2.s -O2
```

## Smoke Test

```bash
scripts/run_smoke.sh
```

## Regression

```bash
# Compile-only regression
scripts/regression.sh test/custom riscv O0
scripts/regression.sh test/custom riscv O1
scripts/regression.sh test/custom riscv O2
scripts/regression.sh test/custom arm O0
scripts/regression.sh test/custom arm O1
scripts/regression.sh test/custom arm O2

# Semantic compare (interpreter vs expected output)
scripts/compare.sh test/custom riscv O1
scripts/compare.sh test/custom arm O1
scripts/compare.sh test/custom riscv O2
scripts/compare.sh test/custom arm O2
# include perf/* cases explicitly when needed
COMPARE_INCLUDE_PERF=1 scripts/compare.sh tests/external/compiler-dev-test-cases/testcases riscv O1

# Fast O0/O1 assembly-size proxy
scripts/asm-delta.sh test/custom riscv

# Matrix evaluation for O1 tuning candidates
scripts/eval-o1-matrix.sh test/custom

# Unified O1/O2 matrix evaluation (RISC-V proxy + ARM consistency checks)
scripts/eval-profile-matrix.sh test/custom

# Sync public suites and generate suite index
scripts/suite-sync.sh --update
scripts/suite-index.sh

# Generate compiler-dev reference outputs (clang baseline)
scripts/gen-reference-out.sh compiler-dev

# Runtime eval (Docker-first, QEMU user-mode)
scripts/eval-runtime.sh open-functional riscv O1
scripts/eval-runtime.sh compiler-dev arm O1

# Compare against local biframe compiler
scripts/eval-vs-biframe.sh open-functional riscv O1

# Official dataset adapter (safe-skip if dirs are absent)
scripts/eval-official-adapter.sh /path/to/official/functional /path/to/official/perf /path/to/runtime
```

## Runtime

A local runtime library is provided in `runtime/sylib.c` and `runtime/sylib.h`.

Runtime evaluation environment variables:

- `SISY_DOCKER_IMAGE` (default: `sisyphus/compiler-dev-dual:latest`)
- `BIFRAME_COMPILER` (default: `/home/wslootie/github/cpe/biframe/build/sysc`)
- `RUNTIME_CASE_LIMIT` / `RUNTIME_CASE_FILTER` (optional smoke/debug subset controls)

Compare/validator environment variables:

- `COMPARE_TIMEOUT_SEC` (default: `30`, per-case compiler compare timeout)
- `COMPARE_INCLUDE_PERF` (default: `0`, skip `perf/*` in `scripts/compare.sh`)
- `SISY_EXEC_STEP_LIMIT` (default: `20000000`, interpreter step budget used by `--compare`)

## QEMU

- AArch64 launcher: `scripts/qemu-aarch64.sh`
- RISC-V launcher: `scripts/qemu-riscv64.sh`
- Workflow details: `docs/QEMU.md`

## Public Suites

- `open-functional`: hard gate (official functional tests from `open-test-cases/sysy`)
- `open-perf`: hard gate (official public/private perf sets from `open-test-cases/sysy`)
- `compiler-dev`: hard gate (reference outputs generated into `tests/external/.refs/compiler-dev`)
- `lvx`: soft gate (runs and reports, not blocking by default)

## Design Docs

- `docs/Design.md`
- `docs/Compliance.md`

## Notes

This repository is built as a standalone implementation. Other repositories are used only as design references.
