# Sisyphus Compiler Design

## Overview

Sisyphus is a SysY compiler for both ARM64 (AArch64) and RISC-V (rv64gc). It uses a unified frontend and IR pipeline, then lowers to architecture-specific backends.

Build target binary name is `compiler`.

## CLI Contract

Mandatory competition-compatible invocations:

```bash
compiler testcase.sy -S -o testcase.s
compiler testcase.sy -S -o testcase.s -O1
```

Additional controls:

- `--target=riscv|arm` (default from CMake `DEFAULT_TARGET`)
- `-O0` and `-O1`
- `--emit-ir`
- `--dump-pass-timing`
- `--verify-ir`

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

## Backends

### RISC-V

- Target ISA: rv64gc
- Dedicated lowering, combine, DCE, register allocation, assembly dump
- Assembly generation designed for large-address execution environments used by contest toolchains

### ARM64

- Target ISA: ARMv8-A AArch64
- Dedicated lowering, combine, ARM-specific cleanup, post-increment legalization, register allocation, assembly dump

## Validation

- `scripts/run_smoke.sh` compiles smoke cases for both targets
- `--verify-ir` runs IR verifier after SSA phase
- `--dump-pass-timing` provides per-pass timing for optimization tuning
