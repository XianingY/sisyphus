#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 <suite> <target> [perf_timeout_sec]"
  echo "suite: open-functional | open-perf | compiler-dev | lvx"
  echo "target: riscv | arm"
  exit 1
fi

SUITE="$1"
TARGET="$2"
PERF_TIMEOUT_SEC="${3:-20}"
AGGR_MEDIAN_TOL_RATIO="${AGGR_MEDIAN_TOL_RATIO:-0.01}"
AGGR_MEDIAN_TOL_MS="${AGGR_MEDIAN_TOL_MS:-0.05}"
AGGR_ENFORCE_NO_EXPAND="${AGGR_ENFORCE_NO_EXPAND:-auto}"

if [[ "${TARGET}" != "riscv" && "${TARGET}" != "arm" ]]; then
  echo "error: target must be riscv|arm"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVAL_RUNTIME="${ROOT_DIR}/scripts/eval-runtime.sh"
OUT_DIR="${ROOT_DIR}/.runtime-reports/aggressive"
mkdir -p "${OUT_DIR}"

TS="$(date +%Y%m%d-%H%M%S)"
O1_CSV="${OUT_DIR}/${SUITE}-${TARGET}-O1-${TS}.csv"
O2_CSV="${OUT_DIR}/${SUITE}-${TARGET}-O2-${TS}.csv"
REGRESSED_TOP20="${OUT_DIR}/${SUITE}-${TARGET}-regressed-top20-${TS}.csv"
REPORT_CSV="${OUT_DIR}/${SUITE}-${TARGET}-report-${TS}.csv"
STATE_FILE="${OUT_DIR}/${SUITE}-${TARGET}.state"

run_eval() {
  local opt="$1"
  local csv="$2"
  RUNTIME_SOFT_PERF=1 \
  RUNTIME_PERF_TIMEOUT_SEC="${PERF_TIMEOUT_SEC}" \
  RUNTIME_CSV="${csv}" \
    "${EVAL_RUNTIME}" "${SUITE}" "${TARGET}" "${opt}"
}

calc_pass_rate() {
  local csv="$1"
  awk -F, '
    NR > 1 { total++; if ($8 == "1") pass++; }
    END {
      if (total == 0) { printf "0.000000"; exit; }
      printf "%.6f", pass / total;
    }
  ' "${csv}"
}

calc_timeout_count() {
  local csv="$1"
  awk -F, 'NR > 1 && $6 == "timeout" { c++ } END { print c + 0 }' "${csv}"
}

calc_functional_fail() {
  local csv="$1"
  awk -F, '
    NR > 1 && index($2, "perf/") != 1 {
      total++;
      if ($8 != "1") fail++;
    }
    END { print fail + 0 }
  ' "${csv}"
}

calc_quantile_ms() {
  local csv="$1"
  local q="$2"
  awk -F, 'NR > 1 && $8 == "1" && $9 != "" { print $9 }' "${csv}" \
    | sort -n \
    | awk -v q="${q}" '
      { a[++n] = $1 }
      END {
        if (n == 0) { printf "0.000"; exit; }
        idx = int((n - 1) * q + 0.999999) + 1;
        if (idx < 1) idx = 1;
        if (idx > n) idx = n;
        printf "%.3f", a[idx];
      }
    '
}

float_leq() {
  local lhs="$1"
  local rhs="$2"
  awk -v a="${lhs}" -v b="${rhs}" 'BEGIN { exit (a <= b ? 0 : 1) }'
}

float_leq_with_tol() {
  local lhs="$1"
  local rhs="$2"
  local tol_ratio="$3"
  local tol_ms="$4"
  awk -v a="${lhs}" -v b="${rhs}" -v tr="${tol_ratio}" -v tm="${tol_ms}" '
    BEGIN {
      limit = b * (1.0 + tr) + tm;
      exit (a <= limit ? 0 : 1);
    }'
}

calc_regressions() {
  local o1_csv="$1"
  local o2_csv="$2"
  local top20_csv="$3"
  local tmp
  tmp="$(mktemp -d)"
  trap 'rm -rf "${tmp}"' RETURN

  awk -F, 'NR > 1 && $8 == "1" && $9 != "" { print $2","$9 }' "${o1_csv}" | sort >"${tmp}/o1.txt"
  awk -F, 'NR > 1 && $8 == "1" && $9 != "" { print $2","$9 }' "${o2_csv}" | sort >"${tmp}/o2.txt"

  join -t, -j 1 "${tmp}/o1.txt" "${tmp}/o2.txt" \
    | awk -F, '
        {
          d = $3 - $2;
          if (d > 0)
            printf "%s,%.3f,%.3f,%.3f\n", $1, $2, $3, d;
        }
      ' \
    | sort -t, -k4,4nr >"${tmp}/regressed.txt"

  {
    echo "case_id,o1_ms,o2_ms,delta_ms"
    head -n 20 "${tmp}/regressed.txt"
  } >"${top20_csv}"

  local regressed_count positive_sum
  regressed_count="$(wc -l <"${tmp}/regressed.txt" | tr -d ' ')"
  positive_sum="$(awk -F, '{ s += $4 } END { printf "%.3f", s + 0 }' "${tmp}/regressed.txt")"
  echo "${regressed_count} ${positive_sum}"
}

echo "[aggressive] run O1 ${SUITE} ${TARGET} (perf_timeout=${PERF_TIMEOUT_SEC}s)"
run_eval O1 "${O1_CSV}"
echo "[aggressive] run O2 ${SUITE} ${TARGET} (perf_timeout=${PERF_TIMEOUT_SEC}s)"
run_eval O2 "${O2_CSV}"

O1_PASS_RATE="$(calc_pass_rate "${O1_CSV}")"
O2_PASS_RATE="$(calc_pass_rate "${O2_CSV}")"
O1_TIMEOUTS="$(calc_timeout_count "${O1_CSV}")"
O2_TIMEOUTS="$(calc_timeout_count "${O2_CSV}")"
O1_FUNC_FAIL="$(calc_functional_fail "${O1_CSV}")"
O2_FUNC_FAIL="$(calc_functional_fail "${O2_CSV}")"
O1_MEDIAN="$(calc_quantile_ms "${O1_CSV}" 0.5)"
O2_MEDIAN="$(calc_quantile_ms "${O2_CSV}" 0.5)"
O1_P90="$(calc_quantile_ms "${O1_CSV}" 0.9)"
O2_P90="$(calc_quantile_ms "${O2_CSV}" 0.9)"
read -r REGRESSED_COUNT POSITIVE_SUM <<<"$(calc_regressions "${O1_CSV}" "${O2_CSV}" "${REGRESSED_TOP20}")"

O2_DELTA_MS="$(awk -v a="${O2_MEDIAN}" -v b="${O1_MEDIAN}" 'BEGIN { printf "%.3f", a - b }')"
O2_DELTA_RATIO="$(awk -v a="${O2_MEDIAN}" -v b="${O1_MEDIAN}" 'BEGIN { if (b <= 0) printf "0.000000"; else printf "%.6f", a / b }')"

TOP20_NO_EXPAND=1
PREV_REGRESSED_COUNT=-1
enforce_no_expand=0
if [[ "${AGGR_ENFORCE_NO_EXPAND}" == "1" ]]; then
  enforce_no_expand=1
elif [[ "${AGGR_ENFORCE_NO_EXPAND}" == "auto" && "${SUITE}" == "compiler-dev" ]]; then
  enforce_no_expand=1
fi
if (( enforce_no_expand == 1 )); then
  if [[ -f "${STATE_FILE}" ]]; then
    PREV_REGRESSED_COUNT="$(awk -F= '$1=="regressed_count" { print $2 }' "${STATE_FILE}" | tail -n 1)"
    if [[ -n "${PREV_REGRESSED_COUNT}" ]] && [[ "${PREV_REGRESSED_COUNT}" != "-1" ]]; then
      if (( REGRESSED_COUNT > PREV_REGRESSED_COUNT )); then
        TOP20_NO_EXPAND=0
      fi
    fi
  fi
fi

FUNCTIONAL_HARD_GATE=1
if (( O1_FUNC_FAIL != 0 || O2_FUNC_FAIL != 0 )); then
  FUNCTIONAL_HARD_GATE=0
fi

SOFT_GATE=1
enforce_perf_soft=0
if [[ "${SUITE}" == "compiler-dev" || "${SUITE}" == "open-perf" || "${SUITE}" == "lvx" ]]; then
  enforce_perf_soft=1
fi
if (( enforce_perf_soft == 1 )); then
  if (( O2_TIMEOUTS > O1_TIMEOUTS )); then
    SOFT_GATE=0
  fi
  if ! float_leq_with_tol "${O2_MEDIAN}" "${O1_MEDIAN}" "${AGGR_MEDIAN_TOL_RATIO}" "${AGGR_MEDIAN_TOL_MS}"; then
    SOFT_GATE=0
  fi
  if (( TOP20_NO_EXPAND == 0 )); then
    SOFT_GATE=0
  fi
fi

printf 'suite,target,perf_timeout_sec,o1_pass_rate,o1_median_ms,o1_p90_ms,o1_timeout_count,o2_pass_rate,o2_median_ms,o2_p90_ms,o2_timeout_count,o2_vs_o1_delta_ms,o2_vs_o1_ratio,regressed_count,positive_sum,functional_hard_gate,soft_gate,top20_no_expand\n' >"${REPORT_CSV}"
printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
  "${SUITE}" "${TARGET}" "${PERF_TIMEOUT_SEC}" \
  "${O1_PASS_RATE}" "${O1_MEDIAN}" "${O1_P90}" "${O1_TIMEOUTS}" \
  "${O2_PASS_RATE}" "${O2_MEDIAN}" "${O2_P90}" "${O2_TIMEOUTS}" \
  "${O2_DELTA_MS}" "${O2_DELTA_RATIO}" "${REGRESSED_COUNT}" "${POSITIVE_SUM}" \
  "${FUNCTIONAL_HARD_GATE}" "${SOFT_GATE}" "${TOP20_NO_EXPAND}" >>"${REPORT_CSV}"

{
  echo "timestamp=${TS}"
  echo "suite=${SUITE}"
  echo "target=${TARGET}"
  echo "perf_timeout_sec=${PERF_TIMEOUT_SEC}"
  echo "o1_csv=${O1_CSV}"
  echo "o2_csv=${O2_CSV}"
  echo "regressed_top20=${REGRESSED_TOP20}"
  echo "report_csv=${REPORT_CSV}"
  echo "regressed_count=${REGRESSED_COUNT}"
  echo "positive_sum=${POSITIVE_SUM}"
} >"${STATE_FILE}"

echo "report: ${REPORT_CSV}"
echo "top20 : ${REGRESSED_TOP20}"
echo "O1    : pass_rate=${O1_PASS_RATE} median=${O1_MEDIAN}ms p90=${O1_P90}ms timeout=${O1_TIMEOUTS}"
echo "O2    : pass_rate=${O2_PASS_RATE} median=${O2_MEDIAN}ms p90=${O2_P90}ms timeout=${O2_TIMEOUTS}"
echo "delta : o2_vs_o1_delta_ms=${O2_DELTA_MS} ratio=${O2_DELTA_RATIO}"
echo "tol   : median_tol_ratio=${AGGR_MEDIAN_TOL_RATIO} median_tol_ms=${AGGR_MEDIAN_TOL_MS}"
echo "rule  : enforce_no_expand=${enforce_no_expand}"
echo "rule  : enforce_perf_soft=${enforce_perf_soft}"
echo "gate  : functional=${FUNCTIONAL_HARD_GATE} soft=${SOFT_GATE} top20_no_expand=${TOP20_NO_EXPAND}"

if (( FUNCTIONAL_HARD_GATE == 0 )); then
  exit 1
fi

# Keep perf as "soft" globally, but fail this tuning script so aggressive changes can be auto-reverted.
if (( SOFT_GATE == 0 )); then
  exit 2
fi
