#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVAL_RUNTIME="${ROOT_DIR}/scripts/eval-runtime.sh"
TARGET="${1:-arm}"
OPT="${2:-O2}"
PERF_TIMEOUT_SEC="${3:-20}"
HOTSPOT_INCLUDE_FINAL="${HOTSPOT_INCLUDE_FINAL:-0}"
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
PERF_SUITE="official-${TARGET}-perf"
FINAL_SUITE="official-${TARGET}-final-perf"
FUNC_SUITE="official-functional"

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
    awk -F, '
NR > 1 {
  fp[$23]++;
  reasons = $24;
  if (NF > 24) {
    for (i = 25; i <= NF; i++)
      reasons = reasons "," $i;
  }
  if (reasons == "" || reasons == "none")
    next;
  n = split(reasons, arr, ",");
  for (i = 1; i <= n; i++) {
    if (arr[i] != "")
      fr[arr[i]]++;
  }
}
END {
  for (k in fp)
    printf "  frontend_path[%s]=%d\n", k, fp[k];
  for (k in fr)
    printf "  fallback_reason[%s]=%d\n", k, fr[k];
}
' "${csv}" | sort
  } | tee -a "${SUMMARY}"
}

# Functional correctness hotspot families.
run_case_group "${FUNC_SUITE}" "95_float" "float-math-95_float"
run_case_group "${FUNC_SUITE}" "35_math" "float-math-35_math"
run_case_group "${FUNC_SUITE}" "37_dct" "float-math-37_dct"
run_case_group "${FUNC_SUITE}" "39_fp_params" "float-math-39_fp_params"

# Performance hotspot families.
run_case_group "${PERF_SUITE}" "03_sort2" "sort-brainfuck-sort2"
run_case_group "${PERF_SUITE}" "brainfuck" "sort-brainfuck-brainfuck"
run_case_group "${PERF_SUITE}" "fft" "fft-crypto-fft"
run_case_group "${PERF_SUITE}" "crypto" "fft-crypto-crypto"
if [[ "${HOTSPOT_INCLUDE_FINAL}" == "1" ]]; then
  run_case_group "${FINAL_SUITE}" "" "final-full"
fi

echo "summary: ${SUMMARY}"
