#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVAL_RUNTIME="${ROOT_DIR}/scripts/eval-runtime.sh"
TARGET="${1:-arm}"
OPT="${2:-O2}"
PERF_TIMEOUT_SEC="${3:-20}"
OUT_DIR="${ROOT_DIR}/.runtime-reports/hotspots"
mkdir -p "${OUT_DIR}"

if [[ "${TARGET}" != "arm" && "${TARGET}" != "riscv" ]]; then
  echo "error: target must be arm|riscv"
  exit 1
fi
if [[ "${OPT}" != "O1" && "${OPT}" != "O2" ]]; then
  echo "error: opt must be O1|O2"
  exit 1
fi

TS="$(date +%Y%m%d-%H%M%S)"
SUMMARY="${OUT_DIR}/summary-${TARGET}-${OPT}-${TS}.txt"
: >"${SUMMARY}"

run_case_group() {
  local suite="$1"
  local filter="$2"
  local name="$3"
  local csv="${OUT_DIR}/${suite}-${TARGET}-${OPT}-${name}-${TS}.csv"

  echo "[hotspot] ${suite} ${TARGET} ${OPT} filter='${filter}'"
  RUNTIME_SOFT_PERF=1 \
  RUNTIME_PERF_TIMEOUT_SEC="${PERF_TIMEOUT_SEC}" \
  RUNTIME_CASE_FILTER="${filter}" \
  RUNTIME_CSV="${csv}" \
    "${EVAL_RUNTIME}" "${suite}" "${TARGET}" "${OPT}" >/dev/null 2>&1 || true

  read -r total pass timeout compile_fail compile_crash link_fail <<EOF
$(awk -F, '
NR > 1 {
  total++;
  if ($8 == "1") pass++;
  if ($6 == "timeout") timeout++;
  if ($6 == "compile_fail") compile_fail++;
  if ($6 == "compile_crash") compile_crash++;
  if ($6 == "link_fail") link_fail++;
}
END {
  printf "%d %d %d %d %d %d\n",
    total + 0, pass + 0, timeout + 0, compile_fail + 0, compile_crash + 0, link_fail + 0;
}
' "${csv}")
EOF

  {
    echo "${suite}/${name}: total=${total} pass=${pass} timeout=${timeout} compile_fail=${compile_fail} compile_crash=${compile_crash} link_fail=${link_fail}"
    awk -F, 'NR > 1 { printf "  %s status=%s pass=%s median=%s\n", $2, $6, $8, $9 }' "${csv}"
  } | tee -a "${SUMMARY}"
}

run_case_group open-perf "median" "median"
run_case_group open-perf "brainfuck" "brainfuck"
run_case_group compiler-dev "perf/12_fft" "fft0"
run_case_group compiler-dev "perf/13_fft" "fft1"
run_case_group compiler-dev "perf/14_fft" "fft2"
run_case_group compiler-dev "perf/18_brainfuck" "brainfuck-bootstrap"
run_case_group compiler-dev "perf/19_brainfuck" "brainfuck-calculator"

echo "summary: ${SUMMARY}"
