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
