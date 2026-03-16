#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DEFAULT_TARGET="${DEFAULT_TARGET:-riscv}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DDEFAULT_TARGET="${DEFAULT_TARGET}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "Built: ${BUILD_DIR}/compiler"
