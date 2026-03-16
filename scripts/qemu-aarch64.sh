#!/usr/bin/env bash
set -euo pipefail

IMG="${1:-noble-server-cloudimg-arm64.img}"
EFI_IMG="${2:-efi.img}"
VARSTORE_IMG="${3:-varstore.img}"
USER_DATA_IMG="${4:-user-data.img}"

qemu-system-aarch64 \
  -m 4096 \
  -smp 4 \
  -cpu max \
  -M virt \
  -nographic \
  -drive if=pflash,format=raw,file="${EFI_IMG}",readonly=on \
  -drive if=pflash,format=raw,file="${VARSTORE_IMG}" \
  -drive if=none,file="${IMG}",id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -drive file="${USER_DATA_IMG}" \
  -netdev user,id=eth0,hostfwd=tcp::2222-:22 \
  -device virtio-net-device,netdev=eth0
