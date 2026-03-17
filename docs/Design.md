# Sisyphus Compiler Design

## Overview

Sisyphus is a SysY compiler for both ARM64 (AArch64) and RISC-V (rv64gc). It uses a unified frontend and IR pipeline, then lowers to architecture-specific backends.

Build target binary name is `compiler`.

## CLI Contract

Mandatory competition-compatible invocations:

```bash
compiler testcase.sy -S -o testcase.s
compiler testcase.sy -S -o testcase.s -O1
compiler testcase.sy -S -o testcase.s -O2
```

Additional controls:

- `--target=riscv|arm` (default from CMake `DEFAULT_TARGET`)
- `-O0`, `-O1`, and `-O2`
- `--emit-ir`
- `--dump-pass-timing`
- `--verify-ir`
- `--inline-threshold`, `--late-inline-threshold`
- `--disable-loop-rotate`, `--enable-loop-rotate`, `--disable-const-unroll`
- `--enable-experimental` (kept opt-in)

## Pipeline

### O0 (stable baseline)

- MoveAlloca
- EarlyConstFold / Pureness
- RaiseToFor / DCE / Lower
- FlattenCFG
- Mem2Reg
- RegularFold / DCE / SimplifyCFG / Select / InstSchedule
- Target lowering (ARM or RISC-V)

### O1 (performance pipeline)

- Structured CFG optimization: const fold, inlining, loop cleanup, memory tidy, scalar transforms
- FlattenCFG + SSA conversion (Mem2Reg)
- Alias + DSE + DLE + DAE + GVN + LICM + loop canonicalization
- Late inline and cleanup rounds
- Final schedule + target backend passes

### O2 (aggressive profile on top of O1)

- Structured stage: enable `Fusion` and `Unswitch`
- Mem2Reg stage: add `Reassociate`
- Select stage: enable `Range + RangeAwareFold + Splice` by default
- Tail stage: add one more `CanonicalizeLoop -> LICM -> SCEV -> GVN -> RegularFold`
- `Cached` remains experimental-only (`--enable-experimental`)
- Defaults: inline/late-inline thresholds are `256/256` unless explicitly overridden; loop-rotate is off by default for O1/O2 (can be manually enabled)

## Backends

### RISC-V

- Target ISA: rv64gc
- Dedicated lowering, combine, DCE, register allocation, assembly dump
- Assembly generation designed for large-address execution environments used by contest toolchains
- Extra low-risk peephole cleanup in regalloc phase:
  - redundant `mv/li/addi` to zero register elimination
  - adjacent address-add plus load fold for compact addressing

### ARM64

- Target ISA: ARMv8-A AArch64
- Dedicated lowering, combine, ARM-specific cleanup, post-increment legalization, register allocation, assembly dump
- Extra low-risk peephole cleanup in regalloc phase:
  - redundant `mov` and writes to `xzr` elimination
  - adjacent `add` + load/store address-offset folding with conservative guards

## Validation

- `scripts/run_smoke.sh` compiles smoke cases for both targets
- `scripts/regression.sh` and `scripts/compare.sh` validate `O0/O1/O2` on custom suites
- `--compare` semantic guard is executed on stabilized pre-backend checkpoint (`inst-schedule`)
- `scripts/compare.sh` defaults to functional-style gate (skips `perf/*` unless `COMPARE_INCLUDE_PERF=1`)
- `scripts/eval-o1-matrix.sh` keeps O1-only tuning workflow
- `scripts/eval-profile-matrix.sh` evaluates O1/O2 matrices with fixed ranking metrics
- `scripts/eval-official-adapter.sh` connects official functional/perf dirs when provided
- `scripts/suite-sync.sh` and `scripts/suite-index.sh` manage external public suite mirrors and index metadata
- `scripts/gen-reference-out.sh compiler-dev` generates hard-gate references for compiler-dev cases
- `scripts/eval-runtime.sh` executes runtime checks in Docker-first dual-target toolchains
- `scripts/eval-vs-biframe.sh` reports `pass_rate`, ratio metrics and staged thresholds against local biframe
- `--verify-ir` runs IR verifier after SSA phase
- `--dump-pass-timing` provides per-pass timing for optimization tuning
