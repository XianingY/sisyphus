# Local QEMU Debug Workflow

## AArch64 VM

1. Prepare image and firmware as in the contest QEMU guide.
2. Start VM:

```bash
scripts/qemu-aarch64.sh noble-server-cloudimg-arm64.img efi.img varstore.img user-data.img
```

## RISC-V VM

1. Prepare the Ubuntu riscv64 image.
2. Start VM:

```bash
scripts/qemu-riscv64.sh ubuntu-24.04.2-preinstalled-server-riscv64.img
```

## Program Debug

Inside VM:

```bash
sudo apt-get update
sudo apt-get install -y gcc gdb
```

Compile with debug symbol and run GDB:

```bash
gcc -g -o test test.c
gdb ./test
```
