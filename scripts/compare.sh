#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <case_dir> [riscv|arm] [O0|O1] [extra compiler args...]"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${ROOT_DIR}/build/compiler"
CASE_DIR="$1"
TARGET="${2:-riscv}"
OPT="${3:-O1}"
EXTRA_ARGS=("${@:4}")
TAG="${OUT_TAG:-}"
OUT_DIR="${ROOT_DIR}/tests/.out/compare-${TARGET}-${OPT}"
if [[ -n "${TAG}" ]]; then
  OUT_DIR="${OUT_DIR}-${TAG}"
fi

mkdir -p "${OUT_DIR}"

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

pass=0
fail=0

for f in "${CASE_DIR}"/*.sy; do
  [[ -f "${f}" ]] || continue
  base="$(basename "${f}" .sy)"
  out="${CASE_DIR}/${base}.out"
  in="${CASE_DIR}/${base}.in"
  log="${OUT_DIR}/${base}.log"

  if [[ ! -f "${out}" ]]; then
    echo "[skip] ${base}: missing ${out}"
    continue
  fi

  cmd=("${COMPILER}" "${f}" -S -o "${OUT_DIR}/${base}.s" "--target=${TARGET}" "-${OPT}" "${EXTRA_ARGS[@]}" --compare "${out}")
  if [[ -f "${in}" ]]; then
    cmd+=(-i "${in}")
  fi

  if "${cmd[@]}" >"${log}" 2>&1; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    echo "[fail] ${base} (log: ${log})"
    tail -n 20 "${log}" || true
  fi
done

echo "Compare summary: pass=${pass}, fail=${fail}, target=${TARGET}, opt=${OPT}"
if [[ ${fail} -ne 0 ]]; then
  exit 1
fi
