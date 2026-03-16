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
- Debug and tuning flags:
  - `--target=riscv|arm`
  - `--emit-ir`
  - `--verify-ir`
  - `--dump-pass-timing`
  - `--enable-experimental` (opt-in for experimental O1 passes; off by default)
  - `--inline-threshold=<N>`
  - `--late-inline-threshold=<N>`
  - `--disable-loop-rotate`
  - `--disable-const-unroll`

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
scripts/regression.sh test/custom arm O0
scripts/regression.sh test/custom arm O1

# Semantic compare (interpreter vs expected output)
scripts/compare.sh test/custom riscv O1
scripts/compare.sh test/custom arm O1

# Fast O0/O1 assembly-size proxy
scripts/asm-delta.sh test/custom riscv

# Matrix evaluation for O1 tuning candidates
scripts/eval-o1-matrix.sh test/custom
```

## Runtime

A local runtime library is provided in `runtime/sylib.c` and `runtime/sylib.h`.

## QEMU

- AArch64 launcher: `scripts/qemu-aarch64.sh`
- RISC-V launcher: `scripts/qemu-riscv64.sh`
- Workflow details: `docs/QEMU.md`

## Design Docs

- `docs/Design.md`
- `docs/Compliance.md`

## Notes

This repository is built as a standalone implementation. Other repositories are used only as design references.
