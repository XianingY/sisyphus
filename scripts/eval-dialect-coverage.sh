#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <suite> <target> <opt>"
  echo "suite: official-functional | official-arm-perf | official-riscv-perf | official-arm-final-perf | official-riscv-final-perf"
  echo "target: riscv | arm"
  echo "opt: O0 | O1 | O2"
  exit 1
fi

SUITE="$1"
TARGET="$2"
OPT="$3"

for s in \
  official-functional \
  official-arm-perf \
  official-riscv-perf \
  official-arm-final-perf \
  official-riscv-final-perf; do
  if [[ "${SUITE}" == "${s}" ]]; then
    supported_suite=1
    break
  fi
done
if [[ "${supported_suite:-0}" -ne 1 ]]; then
  echo "error: unsupported suite '${SUITE}'"
  exit 1
fi

if [[ "${TARGET}" != "riscv" && "${TARGET}" != "arm" ]]; then
  echo "error: target must be riscv|arm"
  exit 1
fi
if [[ "${OPT}" != "O0" && "${OPT}" != "O1" && "${OPT}" != "O2" ]]; then
  echo "error: opt must be O0|O1|O2"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVAL_RUNTIME="${ROOT_DIR}/scripts/eval-runtime.sh"
TS="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="${ROOT_DIR}/.runtime-reports/dialect-coverage/${TS}"
mkdir -p "${OUT_DIR}"

if [[ ! -x "${EVAL_RUNTIME}" ]]; then
  echo "error: missing ${EVAL_RUNTIME}"
  exit 1
fi

SOFT_PERF=0
if [[ "${SUITE}" != "official-functional" ]]; then
  SOFT_PERF=1
fi

run_mode() {
  local mode="$1"
  local extra_args="$2"
  local csv="${OUT_DIR}/${mode}.csv"
  local rc=0

  echo "[dialect-coverage] mode=${mode} suite=${SUITE} target=${TARGET} opt=${OPT}"
  set +e
  RUNTIME_SOFT_PERF="${SOFT_PERF}" \
  RUNTIME_CSV="${csv}" \
  SISY_COMPILER_EXTRA_ARGS="${extra_args}" \
    "${EVAL_RUNTIME}" "${SUITE}" "${TARGET}" "${OPT}"
  rc=$?
  set -e
  echo "${rc}" >"${OUT_DIR}/${mode}.rc"
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

summarize_cluster() {
  local mode="$1"
  local cluster="$2"
  local pattern="$3"
  local csv="${OUT_DIR}/${mode}.csv"

  awk -F, -v mode="${mode}" -v cluster="${cluster}" -v pattern="${pattern}" '
BEGIN {
  total=0; ok=0; mismatch=0; timeout=0; compile_fail=0; compile_crash=0; link_fail=0;
}
NR == 1 { next }
$2 ~ pattern {
  total++;
  if ($6 == "ok") ok++;
  else if ($6 == "mismatch") mismatch++;
  else if ($6 == "timeout") timeout++;
  else if ($6 == "compile_fail") compile_fail++;
  else if ($6 == "compile_crash") compile_crash++;
  else if ($6 == "link_fail") link_fail++;
}
END {
  printf "%s,%s,%d,%d,%d,%d,%d,%d,%d\n",
    mode, cluster, total, ok, mismatch, timeout, compile_fail, compile_crash, link_fail;
}
' "${csv}" >>"${OUT_DIR}/cluster-summary.csv"
}

summarize_mode() {
  local mode="$1"
  local csv="${OUT_DIR}/${mode}.csv"
  local median p90
  median="$(calc_quantile_ms "${csv}" 0.5)"
  p90="$(calc_quantile_ms "${csv}" 0.9)"

  read -r total pass fail timeout mismatch compile_fail compile_crash link_fail pass_rate <<<"$(awk -F, '
BEGIN {
  total=0; pass=0; fail=0;
  timeout=0; mismatch=0; compile_fail=0; compile_crash=0; link_fail=0;
}
NR == 1 { next }
{
  total++;
  ok = ($8 == "1");
  if (ok) pass++; else fail++;
  if ($6 == "timeout") timeout++;
  if ($6 == "mismatch" || $7 == "fail") mismatch++;
  if ($6 == "compile_fail") compile_fail++;
  if ($6 == "compile_crash") compile_crash++;
  if ($6 == "link_fail") link_fail++;
}
END {
  pass_rate = (total == 0 ? 0 : pass / total);
  printf "%d %d %d %d %d %d %d %d %.6f\n",
    total, pass, fail, timeout, mismatch, compile_fail, compile_crash, link_fail, pass_rate;
}
' "${csv}")"

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%.6f,%s,%s\n' \
    "${mode}" "${SUITE}" "${TARGET}" "${OPT}" "${total}" "${pass}" "${fail}" \
    "${timeout}" "${mismatch}" "${compile_fail}" "${compile_crash}" "${link_fail}" \
    "${pass_rate}" "${median}" "${p90}" >>"${OUT_DIR}/summary.csv"

  awk -F, 'NR > 1 { c[$23]++ } END { for (k in c) printf "%s,%s,%d\n", "'"${mode}"'", k, c[k] }' "${csv}" \
    | sort >>"${OUT_DIR}/frontend-path.csv"

  awk -F, '
NR > 1 {
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
      c[arr[i]]++;
  }
}
END {
  for (k in c)
    printf "%s,%s,%d\n", "'"${mode}"'", k, c[k];
}
' "${csv}" | sort >>"${OUT_DIR}/fallback-reasons.csv"

  awk -F, '
NR > 1 {
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
      c[arr[i] "," $2]++;
  }
}
END {
  for (k in c)
    printf "%s,%s,%d\n", "'"${mode}"'", k, c[k];
}
' "${csv}" | sort >>"${OUT_DIR}/fallback-hotspots.csv"
}

run_mode "default" ""
run_mode "forced-dialect" "--force-dialect-codegen"

printf 'mode,suite,target,opt,total,pass,fail,timeout_count,mismatch_count,compile_fail_count,compile_crash_count,link_fail_count,pass_rate,median_ms,p90_ms\n' \
  >"${OUT_DIR}/summary.csv"
printf 'mode,frontend_path,count\n' >"${OUT_DIR}/frontend-path.csv"
printf 'mode,fallback_reason,count\n' >"${OUT_DIR}/fallback-reasons.csv"
printf 'mode,fallback_reason,case_id,count\n' >"${OUT_DIR}/fallback-hotspots.csv"
printf 'mode,cluster,total,ok,mismatch,timeout,compile_fail,compile_crash,link_fail\n' >"${OUT_DIR}/cluster-summary.csv"

summarize_mode "default"
summarize_mode "forced-dialect"
summarize_cluster "default" "ptr_array_cluster" "(04_arr_defn3|05_arr_defn4|61_sort_test7|62_percolation|64_calculator|84_long_array2|88_many_params2|22_matrix_multiply|24_array_only|30_many_dimensions)"
summarize_cluster "forced-dialect" "ptr_array_cluster" "(04_arr_defn3|05_arr_defn4|61_sort_test7|62_percolation|64_calculator|84_long_array2|88_many_params2|22_matrix_multiply|24_array_only|30_many_dimensions)"
summarize_cluster "default" "short_circuit_cluster" "(50_short_circuit|51_short_circuit3|78_side_effect|28_side_effect2)"
summarize_cluster "forced-dialect" "short_circuit_cluster" "(50_short_circuit|51_short_circuit3|78_side_effect|28_side_effect2)"

default_pass="$(awk -F, '$1 == "default" { print $6; exit }' "${OUT_DIR}/summary.csv")"
forced_pass="$(awk -F, '$1 == "forced-dialect" { print $6; exit }' "${OUT_DIR}/summary.csv")"
default_total="$(awk -F, '$1 == "default" { print $5; exit }' "${OUT_DIR}/summary.csv")"
forced_total="$(awk -F, '$1 == "forced-dialect" { print $5; exit }' "${OUT_DIR}/summary.csv")"
printf 'suite,target,opt,default_pass,default_total,forced_pass,forced_total,pass_delta\n' >"${OUT_DIR}/delta.csv"
printf '%s,%s,%s,%s,%s,%s,%s,%d\n' \
  "${SUITE}" "${TARGET}" "${OPT}" \
  "${default_pass:-0}" "${default_total:-0}" \
  "${forced_pass:-0}" "${forced_total:-0}" \
  "$(( ${forced_pass:-0} - ${default_pass:-0} ))" >>"${OUT_DIR}/delta.csv"

echo "report dir: ${OUT_DIR}"
echo "summary: ${OUT_DIR}/summary.csv"
echo "frontend path: ${OUT_DIR}/frontend-path.csv"
echo "fallback reasons: ${OUT_DIR}/fallback-reasons.csv"
echo "fallback hotspots: ${OUT_DIR}/fallback-hotspots.csv"
echo "cluster summary: ${OUT_DIR}/cluster-summary.csv"
echo "delta: ${OUT_DIR}/delta.csv"
