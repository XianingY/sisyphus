#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVAL_RUNTIME="${ROOT_DIR}/scripts/eval-runtime.sh"
RUNTIME_SUMMARY="${ROOT_DIR}/scripts/runtime-summary.sh"

LABEL="${1:-}"
PERF_TIMEOUT_SEC="${2:-20}"
if [[ -z "${LABEL}" ]]; then
  LABEL="$(date +%Y%m%d-%H%M%S)"
fi

commit="$(git -C "${ROOT_DIR}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
stamp="$(date +%Y%m%d-%H%M%S)"
baseline_id="${stamp}-${LABEL}-${commit}"
baseline_root="${ROOT_DIR}/.runtime-reports/baselines/${baseline_id}"
runtime_root="${baseline_root}/runtime"
summary_dir="${baseline_root}/summary"
manifest_csv="${baseline_root}/manifest.csv"

mkdir -p "${runtime_root}" "${summary_dir}"

printf 'label,git_commit,suite,target,opt,status,csv\n' >"${manifest_csv}"
overall_status=0

expected_count_for_suite() {
  local suite="$1"
  case "${suite}" in
    official-functional) echo 140 ;;
    official-arm-perf|official-riscv-perf) echo 59 ;;
    official-arm-final-perf|official-riscv-final-perf) echo 60 ;;
    *)
      echo "error: unknown suite '${suite}' for count assertion" >&2
      return 1
      ;;
  esac
}

run_profile() {
  local suite="$1"
  local target="$2"
  local opt="$3"
  local csv="${runtime_root}/sisyphus-${suite}-${target}-${opt}.csv"
  local status="ok"

  echo "[baseline] ${suite} ${target} ${opt}"
  set +e
  RUNTIME_ROOT="${runtime_root}" \
  RUNTIME_CSV="${csv}" \
  RUNTIME_LABEL="sisyphus" \
  RUNTIME_CASE_LIMIT=0 \
  RUNTIME_CASE_FILTER= \
  RUNTIME_PERF_TIMEOUT_SEC="${PERF_TIMEOUT_SEC}" \
    "${EVAL_RUNTIME}" "${suite}" "${target}" "${opt}"
  local rc=$?
  set -e
  if [[ "${rc}" -ne 0 ]]; then
    status="fail(${rc})"
    overall_status=1
  fi

  local expected_count actual_count
  expected_count="$(expected_count_for_suite "${suite}")"
  if [[ ! -f "${csv}" ]]; then
    status="fail(no_csv)"
    overall_status=1
  else
    actual_count=$(( $(wc -l < "${csv}") - 1 ))
    if [[ "${actual_count}" -ne "${expected_count}" ]]; then
      status="fail(count:${actual_count}/${expected_count})"
      overall_status=1
    fi
  fi
  printf '%s,%s,%s,%s,%s,%s,%s\n' "${baseline_id}" "${commit}" "${suite}" "${target}" "${opt}" "${status}" "${csv}" >>"${manifest_csv}"
}

run_profile official-functional riscv O1
run_profile official-functional arm O1
run_profile official-functional riscv O2
run_profile official-functional arm O2
run_profile official-riscv-perf riscv O1
run_profile official-riscv-perf riscv O2
run_profile official-arm-perf arm O1
run_profile official-arm-perf arm O2
run_profile official-riscv-final-perf riscv O1
run_profile official-riscv-final-perf riscv O2
run_profile official-arm-final-perf arm O1
run_profile official-arm-final-perf arm O2

SUMMARY_DIR="${summary_dir}" \
SUMMARY_LABEL="sisyphus" \
  "${RUNTIME_SUMMARY}" "${runtime_root}"

cat >"${baseline_root}/README.txt" <<EOF
baseline=${baseline_id}
git_commit=${commit}
runtime_root=${runtime_root}
summary_dir=${summary_dir}
manifest=${manifest_csv}
perf_timeout_sec=${PERF_TIMEOUT_SEC}
generated_at=$(date -Iseconds)
EOF

echo "baseline_root=${baseline_root}"
echo "manifest=${manifest_csv}"
echo "summary=${summary_dir}"
exit "${overall_status}"
