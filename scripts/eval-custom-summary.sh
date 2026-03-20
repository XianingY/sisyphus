#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_ROOT="${1:-${ROOT_DIR}/tests/.out/custom-runtime}"
LABEL="${2:-custom-sysy2022}"
OUT_DIR="${ROOT_DIR}/.runtime-reports/custom-summary/$(date +%Y%m%d-%H%M%S)"

if [[ ! -d "${RUNTIME_ROOT}" ]]; then
  echo "error: runtime root not found: ${RUNTIME_ROOT}"
  exit 1
fi

mapfile -t CSVS < <(find "${RUNTIME_ROOT}" -maxdepth 1 -type f -name "${LABEL}-*-O*.csv" | sort)
if [[ "${#CSVS[@]}" -eq 0 ]]; then
  echo "error: no csv found under ${RUNTIME_ROOT} for label=${LABEL}"
  exit 1
fi

mkdir -p "${OUT_DIR}"

printf 'profile,target,opt,total,pass,fail,pass_rate,timeout_count,mismatch_count,compile_fail_count,compile_crash_count,link_fail_count,runtime_crash_count\n' >"${OUT_DIR}/overview.csv"
printf 'profile,target,opt,tier,total,pass,fail,pass_rate\n' >"${OUT_DIR}/tier-summary.csv"
printf 'profile,target,opt,tag,fail_count\n' >"${OUT_DIR}/tag-failure-leaderboard.csv"
printf 'profile,target,opt,frontend_path,count\n' >"${OUT_DIR}/frontend-path.csv"
printf 'profile,target,opt,fallback_reason,count\n' >"${OUT_DIR}/fallback-reasons.csv"

for csv in "${CSVS[@]}"; do
  base="$(basename "${csv}")"
  profile="${base%.csv}"
  rest="${profile#${LABEL}-}"
  opt="${rest##*-}"
  target="${rest%-${opt}}"

  read -r total pass fail timeout_count mismatch_count compile_fail_count compile_crash_count link_fail_count runtime_crash_count pass_rate <<EOF1
$(awk -F, '
NR > 1 {
  total++;
  if ($7 == "1") pass++; else fail++;
  if ($6 == "timeout") timeout_count++;
  if ($6 == "mismatch") mismatch_count++;
  if ($6 == "compile_fail" || $6 == "unexpected_compile_success") compile_fail_count++;
  if ($6 == "compile_crash") compile_crash_count++;
  if ($6 == "link_fail") link_fail_count++;
  if ($6 == "runtime_crash") runtime_crash_count++;
}
END {
  pass_rate = (total == 0 ? 0 : pass / total);
  printf "%d %d %d %d %d %d %d %d %d %.6f\n", total + 0, pass + 0, fail + 0,
    timeout_count + 0, mismatch_count + 0, compile_fail_count + 0, compile_crash_count + 0,
    link_fail_count + 0, runtime_crash_count + 0, pass_rate;
}
' "${csv}")
EOF1

  printf '%s,%s,%s,%s,%s,%s,%.6f,%s,%s,%s,%s,%s,%s\n' \
    "${profile}" "${target}" "${opt}" "${total}" "${pass}" "${fail}" "${pass_rate}" \
    "${timeout_count}" "${mismatch_count}" "${compile_fail_count}" "${compile_crash_count}" \
    "${link_fail_count}" "${runtime_crash_count}" >>"${OUT_DIR}/overview.csv"

  awk -F, -v p="${profile}" -v t="${target}" -v o="${opt}" '
NR > 1 {
  tier = $2;
  total[tier]++;
  if ($7 == "1") pass[tier]++; else fail[tier]++;
}
END {
  for (k in total) {
    pr = (total[k] == 0 ? 0 : pass[k] / total[k]);
    printf "%s,%s,%s,%s,%d,%d,%d,%.6f\n", p, t, o, k, total[k], pass[k] + 0, fail[k] + 0, pr;
  }
}
' "${csv}" | sort >>"${OUT_DIR}/tier-summary.csv"

  awk -F, -v p="${profile}" -v t="${target}" -v o="${opt}" '
NR > 1 {
  if ($7 == "1") next;
  n = split($14, tags, "\\|");
  for (i = 1; i <= n; i++) {
    if (tags[i] != "")
      cnt[tags[i]]++;
  }
}
END {
  for (k in cnt)
    printf "%s,%s,%s,%s,%d\n", p, t, o, k, cnt[k];
}
' "${csv}" | sort -t, -k5,5nr >>"${OUT_DIR}/tag-failure-leaderboard.csv"

  awk -F, -v p="${profile}" -v t="${target}" -v o="${opt}" '
NR > 1 {
  fp = $21;
  if (fp == "") fp = "unknown";
  cnt[fp]++;
}
END {
  for (k in cnt)
    printf "%s,%s,%s,%s,%d\n", p, t, o, k, cnt[k];
}
' "${csv}" | sort >>"${OUT_DIR}/frontend-path.csv"

  awk -F, -v p="${profile}" -v t="${target}" -v o="${opt}" '
NR > 1 {
  reasons = $22;
  if (reasons == "" || reasons == "none") next;
  n = split(reasons, arr, "\\|");
  for (i = 1; i <= n; i++) {
    if (arr[i] != "") cnt[arr[i]]++;
  }
}
END {
  for (k in cnt)
    printf "%s,%s,%s,%s,%d\n", p, t, o, k, cnt[k];
}
' "${csv}" | sort -t, -k5,5nr >>"${OUT_DIR}/fallback-reasons.csv"

done

echo "custom summary dir: ${OUT_DIR}"
echo "overview: ${OUT_DIR}/overview.csv"
echo "tier summary: ${OUT_DIR}/tier-summary.csv"
echo "tag leaderboard: ${OUT_DIR}/tag-failure-leaderboard.csv"
echo "frontend path: ${OUT_DIR}/frontend-path.csv"
echo "fallback reasons: ${OUT_DIR}/fallback-reasons.csv"
