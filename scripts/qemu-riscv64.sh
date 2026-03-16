#!/usr/bin/env bash
set -euo pipefail

IMG="${1:-ubuntu-24.04.2-preinstalled-server-riscv64.img}"

qemu-system-riscv64 \
  -machine virt \
  -nographic \
  -m 2048 \
  -smp 4 \
  -kernel /usr/lib/u-boot/qemu-riscv64_smode/uboot.elf \
  -device virtio-net-device,netdev=eth0 \
  -netdev user,id=eth0,hostfwd=tcp::2222-:22 \
  -device virtio-rng-pci \
  -drive file="${IMG}",format=raw,if=virtio
