#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <case_dir> [riscv|arm] [O0|O1]"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${ROOT_DIR}/build/compiler"
CASE_DIR="$1"
TARGET="${2:-riscv}"
OPT="${3:-O1}"
OUT_DIR="${ROOT_DIR}/tests/.out/${TARGET}-${OPT}"

mkdir -p "${OUT_DIR}"

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

count=0
for f in "${CASE_DIR}"/*.sy; do
  [[ -f "${f}" ]] || continue
  base="$(basename "${f}" .sy)"
  "${COMPILER}" "${f}" -S -o "${OUT_DIR}/${base}.s" "--target=${TARGET}" "-${OPT}"
  count=$((count + 1))
done

echo "Compiled ${count} cases to ${OUT_DIR}"
