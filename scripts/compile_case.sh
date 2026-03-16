#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <input.sy> <output.s> [riscv|arm] [O0|O1]"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${ROOT_DIR}/build/compiler"
INPUT="$1"
OUTPUT="$2"
TARGET="${3:-riscv}"
OPT="${4:-O1}"

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

"${COMPILER}" "${INPUT}" -S -o "${OUTPUT}" "--target=${TARGET}" "-${OPT}"
