#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${ROOT_DIR}/build/compiler"
CASE_ROOT="${ROOT_DIR}/tests/external/compiler-dev-test-cases/testcases/perf"
OUT_DIR="${ROOT_DIR}/tests/.out/arm-o2-fft-regression"
mkdir -p "${OUT_DIR}"

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

if [[ ! -d "${CASE_ROOT}" ]]; then
  echo "missing ${CASE_ROOT}; run scripts/suite-sync.sh --update first"
  exit 1
fi

cases=(
  "12_fft0.c"
  "13_fft1.c"
  "14_fft2.c"
)

for c in "${cases[@]}"; do
  src="${CASE_ROOT}/${c}"
  if [[ ! -f "${src}" ]]; then
    echo "missing case: ${src}"
    exit 1
  fi
  asm="${OUT_DIR}/${c%.c}.arm.o2.s"
  log="${OUT_DIR}/${c%.c}.log"
  echo "[arm-o2-fft] compile ${c}"
  if ! "${COMPILER}" "${src}" -S -o "${asm}" --target=arm -O2 >"${log}" 2>&1; then
    echo "[arm-o2-fft] failed: ${c} (log: ${log})"
    tail -n 40 "${log}" || true
    exit 1
  fi
done

echo "ARM O2 FFT compile regressions passed."
