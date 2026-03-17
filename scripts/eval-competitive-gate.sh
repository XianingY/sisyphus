#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVAL_RUNTIME="${ROOT_DIR}/scripts/eval-runtime.sh"

if [[ ! -x "${EVAL_RUNTIME}" ]]; then
  echo "error: missing ${EVAL_RUNTIME}"
  exit 1
fi

PERF_TIMEOUT_SEC="${1:-20}"
SUMMARY_ONLY="${SUMMARY_ONLY:-0}"
OUT_BASE="${ROOT_DIR}/.runtime-reports/competitive"
TS="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="${OUT_BASE}/${TS}"
RAW_DIR="${OUT_DIR}/raw"
mkdir -p "${RAW_DIR}"

declare -A CSV_PATHS

run_eval() {
  local suite="$1"
  local target="$2"
  local opt="$3"
  local soft_perf="$4"
  local timeout_sec="$5"
  local key="${suite}-${target}-${opt}"
  local csv="${RAW_DIR}/${key}.csv"
  CSV_PATHS["${key}"]="${csv}"

  if [[ "${SUMMARY_ONLY}" == "1" ]]; then
    if [[ ! -f "${csv}" ]]; then
      echo "error: SUMMARY_ONLY=1 but missing ${csv}"
      exit 1
    fi
    return
  fi

  echo "[eval] ${suite} ${target} ${opt} (soft_perf=${soft_perf} timeout=${timeout_sec}s)"
  RUNTIME_SOFT_PERF="${soft_perf}" \
  RUNTIME_PERF_TIMEOUT_SEC="${timeout_sec}" \
  RUNTIME_CSV="${csv}" \
    "${EVAL_RUNTIME}" "${suite}" "${target}" "${opt}" || true
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

summarize_csv() {
  local suite="$1"
  local target="$2"
  local opt="$3"
  local csv="$4"
  local median p90
  median="$(calc_quantile_ms "${csv}" 0.5)"
  p90="$(calc_quantile_ms "${csv}" 0.9)"

  # shellcheck disable=SC2016
  read -r total pass fail timeout mismatch compile_fail compile_crash link_fail pass_rate func_fail <<<"$(awk -F, '
BEGIN {
  total=0; pass=0; fail=0;
  timeout=0; mismatch=0; compile_fail=0; compile_crash=0; link_fail=0;
  func_fail=0;
}
NR == 1 { next }
{
  total++;
  is_pass = ($8 == "1");
  is_perf = (index($2, "perf/") == 1) || ($1 == "open-perf");
  if (is_pass) pass++; else fail++;
  if ($6 == "timeout") timeout++;
  if ($6 == "mismatch" || $7 == "fail") mismatch++;
  if ($6 == "compile_fail") compile_fail++;
  if ($6 == "compile_crash") compile_crash++;
  if ($6 == "link_fail") link_fail++;
  if (!is_perf && !is_pass) func_fail++;
}
END {
  pass_rate = (total == 0 ? 0 : pass / total);
  printf "%d %d %d %d %d %d %d %d %.6f %d\n",
    total, pass, fail, timeout, mismatch, compile_fail, compile_crash, link_fail, pass_rate, func_fail;
}
' "${csv}")"

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%.6f,%s,%s,%s\n' \
    "${suite}" "${target}" "${opt}" \
    "${total}" "${pass}" "${fail}" \
    "${timeout}" "${mismatch}" \
    "${compile_fail}" "${compile_crash}" "${link_fail}" "${pass_rate}" \
    "${median}" "${p90}" "${csv}"
  printf '%s,%s,%s,%s\n' "${suite}" "${target}" "${opt}" "${func_fail}" >>"${OUT_DIR}/functional-fails.csv"
}

emit_regression_top20() {
  local target="$1"
  local o1_csv="$2"
  local o2_csv="$3"
  local out_csv="$4"
  local tmp_dir
  tmp_dir="$(mktemp -d)"

  awk -F, 'NR > 1 && $8 == "1" && $9 != "" { print $2","$9 }' "${o1_csv}" | sort >"${tmp_dir}/o1.txt"
  awk -F, 'NR > 1 && $8 == "1" && $9 != "" { print $2","$9 }' "${o2_csv}" | sort >"${tmp_dir}/o2.txt"

  {
    echo "target,case_id,o1_ms,o2_ms,delta_ms"
    join -t, -j 1 "${tmp_dir}/o1.txt" "${tmp_dir}/o2.txt" \
      | awk -F, -v t="${target}" '
        {
          d = $3 - $2;
          if (d > 0)
            printf "%s,%s,%.3f,%.3f,%.3f\n", t, $1, $2, $3, d;
        }' \
      | sort -t, -k5,5nr | head -n 20
  } >"${out_csv}"
  rm -rf "${tmp_dir}"
}

mkdir -p "${OUT_DIR}"
printf 'suite,target,opt,total,pass,fail,timeout_count,mismatch_count,compile_fail_count,compile_crash_count,link_fail_count,pass_rate,median_ms,p90_ms,csv\n' \
  >"${OUT_DIR}/metrics.csv"
printf 'suite,target,opt,functional_fail\n' >"${OUT_DIR}/functional-fails.csv"

# Hard gate chain.
for target in riscv arm; do
  for opt in O1 O2; do
    run_eval open-functional "${target}" "${opt}" 0 10
    run_eval compiler-dev "${target}" "${opt}" 1 "${PERF_TIMEOUT_SEC}"
  done
done

# Soft perf chain.
for target in riscv arm; do
  for opt in O1 O2; do
    run_eval open-perf "${target}" "${opt}" 1 "${PERF_TIMEOUT_SEC}"
  done
done

for suite in open-functional compiler-dev open-perf; do
  for target in riscv arm; do
    for opt in O1 O2; do
      key="${suite}-${target}-${opt}"
      summarize_csv "${suite}" "${target}" "${opt}" "${CSV_PATHS[$key]}" >>"${OUT_DIR}/metrics.csv"
    done
  done
done

emit_regression_top20 \
  riscv \
  "${CSV_PATHS[open-perf-riscv-O1]}" \
  "${CSV_PATHS[open-perf-riscv-O2]}" \
  "${OUT_DIR}/top-regressions-riscv.csv"
emit_regression_top20 \
  arm \
  "${CSV_PATHS[open-perf-arm-O1]}" \
  "${CSV_PATHS[open-perf-arm-O2]}" \
  "${OUT_DIR}/top-regressions-arm.csv"

{
  echo "group,detail,count"
  for opt in O1 O2; do
    csv="${CSV_PATHS[open-perf-arm-${opt}]}"
    count="$(awk -F, 'NR > 1 && ($2 ~ /crypto/ || $2 ~ /conv/) && ($6 == "mismatch" || $7 == "fail") { c++ } END { print c + 0 }' "${csv}")"
    echo "arm-mismatch(crypto/conv),${opt},${count}"
  done
  for opt in O1 O2; do
    csv="${CSV_PATHS[open-perf-arm-${opt}]}"
    count="$(awk -F, 'NR > 1 && ($2 ~ /03_sort2/ || $2 ~ /brainfuck/) && $6 == "timeout" { c++ } END { print c + 0 }' "${csv}")"
    echo "arm-timeout(03_sort2/brainfuck),${opt},${count}"
  done
  csv="${CSV_PATHS[open-perf-riscv-O1]}"
  count="$(awk -F, 'NR > 1 && $2 ~ /^median/ && ($6 == "mismatch" || $7 == "fail") { c++ } END { print c + 0 }' "${csv}")"
  echo "riscv-o1-median,O1,${count}"
} >"${OUT_DIR}/failure-groups.csv"

functional_fail_total="$(awk -F, 'NR > 1 { s += $4 } END { print s + 0 }' "${OUT_DIR}/functional-fails.csv")"
compile_fail_total="$(awk -F, 'NR > 1 { s += $9 } END { print s + 0 }' "${OUT_DIR}/metrics.csv")"
compile_crash_total="$(awk -F, 'NR > 1 { s += $10 } END { print s + 0 }' "${OUT_DIR}/metrics.csv")"
link_fail_total="$(awk -F, 'NR > 1 { s += $11 } END { print s + 0 }' "${OUT_DIR}/metrics.csv")"

riscv_o1_pass="$(awk -F, '$1 == "open-perf" && $2 == "riscv" && $3 == "O1" { print $5; exit }' "${OUT_DIR}/metrics.csv")"
arm_o2_pass="$(awk -F, '$1 == "open-perf" && $2 == "arm" && $3 == "O2" { print $5; exit }' "${OUT_DIR}/metrics.csv")"
riscv_o1_pass="${riscv_o1_pass:-0}"
arm_o2_pass="${arm_o2_pass:-0}"

hard_gate=1
if (( functional_fail_total != 0 || compile_crash_total != 0 || compile_fail_total != 0 || link_fail_total != 0 )); then
  hard_gate=0
fi

competitive_gate=1
if (( riscv_o1_pass < 58 || arm_o2_pass < 55 )); then
  competitive_gate=0
fi

{
  echo "generated_at=$(date -Iseconds)"
  echo "out_dir=${OUT_DIR}"
  echo "perf_timeout_sec=${PERF_TIMEOUT_SEC}"
  echo "functional_fail_total=${functional_fail_total}"
  echo "compile_fail_total=${compile_fail_total}"
  echo "compile_crash_total=${compile_crash_total}"
  echo "link_fail_total=${link_fail_total}"
  echo "open_perf_riscv_o1_pass=${riscv_o1_pass}"
  echo "open_perf_arm_o2_pass=${arm_o2_pass}"
  if (( hard_gate == 1 )); then
    echo "functional_hard_gate=PASS"
  else
    echo "functional_hard_gate=FAIL"
  fi
  if (( competitive_gate == 1 )); then
    echo "competitive_soft_gate=PASS"
  else
    echo "competitive_soft_gate=FAIL"
  fi
} >"${OUT_DIR}/gate-status.txt"

echo "report dir: ${OUT_DIR}"
echo "metrics: ${OUT_DIR}/metrics.csv"
echo "failure groups: ${OUT_DIR}/failure-groups.csv"
echo "regressions: ${OUT_DIR}/top-regressions-riscv.csv, ${OUT_DIR}/top-regressions-arm.csv"
echo "gate: ${OUT_DIR}/gate-status.txt"

if (( hard_gate == 0 )); then
  exit 1
fi
if (( competitive_gate == 0 )); then
  exit 2
fi
