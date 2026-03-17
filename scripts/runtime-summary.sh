#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_ROOT="${1:-${ROOT_DIR}/tests/.out/runtime}"
SUMMARY_LABEL="${SUMMARY_LABEL:-sisyphus}"
SUMMARY_DIR="${ROOT_DIR}/.runtime-reports/summary"
SUMMARY_GATE_OPTS="${SUMMARY_GATE_OPTS:-O1,O2}"

if [[ -d "${ROOT_DIR}/tests/.out/runtime" && -w "${ROOT_DIR}/tests/.out/runtime" ]]; then
  SUMMARY_DIR="${ROOT_DIR}/tests/.out/runtime/summary"
fi
mkdir -p "${SUMMARY_DIR}"

if [[ ! -d "${RUNTIME_ROOT}" ]]; then
  echo "error: runtime root not found: ${RUNTIME_ROOT}"
  exit 1
fi

find_latest_csvs() {
  local tmp="$1"
  while IFS= read -r f; do
    local base profile mtime
    base="$(basename "${f}")"
    profile="${base#${SUMMARY_LABEL}-}"
    profile="${profile%.csv}"
    mtime="$(stat -c %Y "${f}")"
    printf '%s\t%s\t%s\n' "${profile}" "${mtime}" "${f}" >>"${tmp}"
  done < <(find "${RUNTIME_ROOT}" -maxdepth 1 -type f -name "${SUMMARY_LABEL}-*.csv" | sort)
}

index_tmp="$(mktemp)"
latest_tmp="$(mktemp)"
trap 'rm -f "${index_tmp}" "${latest_tmp}"' EXIT

find_latest_csvs "${index_tmp}"
if [[ ! -s "${index_tmp}" ]]; then
  echo "error: no runtime csv found for label=${SUMMARY_LABEL} under ${RUNTIME_ROOT}"
  exit 1
fi

awk -F'\t' '
{
  p = $1
  ts = $2 + 0
  if (!(p in best) || ts > best[p]) {
    best[p] = ts
    path[p] = $3
  }
}
END {
  for (p in path)
    printf "%s,%s,%s\n", p, best[p], path[p]
}
' "${index_tmp}" | sort >"${latest_tmp}"

printf 'profile,mtime,path\n' >"${SUMMARY_DIR}/latest-index.csv"
cat "${latest_tmp}" >>"${SUMMARY_DIR}/latest-index.csv"

printf 'profile,suite,target,opt,total,pass,fail,functional_total,functional_pass,functional_fail,perf_total,perf_pass,perf_fail,timeout_count,pass_rate,functional_pass_rate,perf_pass_rate,path\n' \
  >"${SUMMARY_DIR}/overview-by-profile.csv"

hard_fail_total=0
while IFS=, read -r profile mtime path; do
  opt="${profile##*-}"
  rest="${profile%-*}"
  target="${rest##*-}"
  suite="${rest%-*}"

  # shellcheck disable=SC2016
  read -r total pass fail ftotal fpass ffail ptotal ppass pfail timeout_count pass_rate fpass_rate ppass_rate <<EOF
$(awk -F, '
BEGIN {
  total=0; pass=0; fail=0;
  ftotal=0; fpass=0; ffail=0;
  ptotal=0; ppass=0; pfail=0;
  timeout_count=0;
}
NR == 1 { next }
{
  total++;
  is_pass = ($8 == "1");
  is_perf = (index($2, "perf/") == 1) || ($1 == "open-perf");
  if (is_pass) pass++; else fail++;
  if ($6 == "timeout") timeout_count++;

  if (is_perf) {
    ptotal++;
    if (is_pass) ppass++; else pfail++;
  } else {
    ftotal++;
    if (is_pass) fpass++; else ffail++;
  }
}
END {
  pass_rate = (total == 0 ? 0 : pass / total);
  fpass_rate = (ftotal == 0 ? 0 : fpass / ftotal);
  ppass_rate = (ptotal == 0 ? 0 : ppass / ptotal);
  printf "%d %d %d %d %d %d %d %d %d %d %.6f %.6f %.6f\n",
    total, pass, fail, ftotal, fpass, ffail, ptotal, ppass, pfail, timeout_count,
    pass_rate, fpass_rate, ppass_rate;
}
' "${path}")
EOF

  gate_this=0
  IFS=',' read -r -a gate_opts <<<"${SUMMARY_GATE_OPTS}"
  for gate_opt in "${gate_opts[@]}"; do
    if [[ "${opt}" == "${gate_opt}" ]]; then
      gate_this=1
      break
    fi
  done
  if [[ "${gate_this}" -eq 1 ]]; then
    hard_fail_total=$((hard_fail_total + ffail))
  fi

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%.6f,%.6f,%.6f,%s\n' \
    "${profile}" "${suite}" "${target}" "${opt}" \
    "${total}" "${pass}" "${fail}" "${ftotal}" "${fpass}" "${ffail}" \
    "${ptotal}" "${ppass}" "${pfail}" "${timeout_count}" \
    "${pass_rate}" "${fpass_rate}" "${ppass_rate}" "${path}" \
    >>"${SUMMARY_DIR}/overview-by-profile.csv"

  out_slowest="${SUMMARY_DIR}/slowest-top20-${profile}.csv"
  printf 'case_id,median_ms,status,compare,pass\n' >"${out_slowest}"
  awk -F, 'NR > 1 && $9 != "" { printf "%s,%s,%s,%s,%s\n", $2, $9, $6, $7, $8 }' "${path}" \
    | sort -t, -k2,2nr | head -n 20 >>"${out_slowest}"

  out_timeout="${SUMMARY_DIR}/timeouts-${profile}.txt"
  awk -F, 'NR > 1 && $6 == "timeout" { printf "%s status=%s pass=%s median=%s log=%s\n", $2, $6, $8, $9, $16 }' "${path}" \
    >"${out_timeout}"
done <"${latest_tmp}"

printf 'suite,target,case_id,o1_ms,o2_ms,delta_ms\n' >"${SUMMARY_DIR}/o2-vs-o1-regressed-top20.csv"
while IFS=, read -r profile mtime path; do
  opt="${profile##*-}"
  rest="${profile%-*}"
  target="${rest##*-}"
  suite="${rest%-*}"
  if [[ "${opt}" != "O1" ]]; then
    continue
  fi
  o2_profile="${suite}-${target}-O2"
  o2_path="$(awk -F, -v p="${o2_profile}" '$1 == p { print $3; exit }' "${latest_tmp}")"
  if [[ -z "${o2_path}" ]]; then
    continue
  fi
  o1_cases="$(awk -F, 'NR > 1 { n++ } END { print n + 0 }' "${path}")"
  o2_cases="$(awk -F, 'NR > 1 { n++ } END { print n + 0 }' "${o2_path}")"
  # Skip O1/O2 deltas when either side is only a filtered/partial sample.
  if (( o1_cases < 20 || o2_cases < 20 )); then
    continue
  fi

  paste <(awk -F, 'NR>1 && $8=="1" && $9!="" { print $2","$9 }' "${path}" | sort) \
        <(awk -F, 'NR>1 && $8=="1" && $9!="" { print $2","$9 }' "${o2_path}" | sort) \
    | awk -F'[,\t]' -v s="${suite}" -v t="${target}" '
      $1 == $3 {
        d = $4 - $2;
        if (d > 0)
          printf "%s,%s,%s,%.3f,%.3f,%.3f\n", s, t, $1, $2, $4, d;
      }' \
    | sort -t, -k6,6nr | head -n 20 >>"${SUMMARY_DIR}/o2-vs-o1-regressed-top20.csv"
done <"${latest_tmp}"

sort -t, -k6,6nr -o "${SUMMARY_DIR}/o2-vs-o1-regressed-top20.csv" "${SUMMARY_DIR}/o2-vs-o1-regressed-top20.csv"

status_file="${SUMMARY_DIR}/gate-status.txt"
{
  echo "label=${SUMMARY_LABEL}"
  echo "runtime_root=${RUNTIME_ROOT}"
  echo "gate_opts=${SUMMARY_GATE_OPTS}"
  echo "generated_at=$(date -Iseconds)"
  echo "hard_fail_total=${hard_fail_total}"
  if [[ "${hard_fail_total}" -eq 0 ]]; then
    echo "functional_hard_gate=PASS"
  else
    echo "functional_hard_gate=FAIL"
  fi
  echo "perf_soft_gate=REPORT_ONLY"
} >"${status_file}"

echo "summary dir: ${SUMMARY_DIR}"
echo "latest index: ${SUMMARY_DIR}/latest-index.csv"
echo "overview: ${SUMMARY_DIR}/overview-by-profile.csv"
echo "regressed top20: ${SUMMARY_DIR}/o2-vs-o1-regressed-top20.csv"
echo "gate status: ${status_file}"

if [[ "${hard_fail_total}" -ne 0 ]]; then
  exit 1
fi
