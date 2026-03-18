#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <suite> <target> <opt>"
  exit 1
fi

SUITE="$1"
TARGET="$2"
OPT="$3"
case "${SUITE}" in
  official-functional|official-arm-perf|official-riscv-perf|official-arm-final-perf|official-riscv-final-perf)
    ;;
  open-functional)
    echo "error: suite '${SUITE}' has been removed; use 'official-functional'"
    exit 1
    ;;
  open-perf)
    echo "error: suite '${SUITE}' has been removed; use one of official-*-perf suites"
    exit 1
    ;;
  compiler-dev|lvx)
    echo "error: suite '${SUITE}' has been removed from baseline"
    exit 1
    ;;
  *)
    echo "error: unsupported suite '${SUITE}'"
    exit 1
    ;;
esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVAL_RUNTIME="${ROOT_DIR}/scripts/eval-runtime.sh"
BIFRAME_COMPILER="${BIFRAME_COMPILER:-/home/wslootie/github/cpe/biframe/build/sysc}"

if [[ ! -x "${EVAL_RUNTIME}" ]]; then
  echo "error: missing ${EVAL_RUNTIME}"
  exit 1
fi
if [[ ! -x "${ROOT_DIR}/build/compiler" ]]; then
  echo "error: missing ${ROOT_DIR}/build/compiler"
  exit 1
fi
if [[ ! -x "${BIFRAME_COMPILER}" ]]; then
  echo "error: missing biframe compiler at ${BIFRAME_COMPILER}"
  exit 1
fi

# eval-runtime may run inside Docker with only ROOT_DIR mounted.
# Mirror biframe binary into this repo so the in-container path is stable.
TOOLS_DIR="${ROOT_DIR}/.runtime-tools"
mkdir -p "${TOOLS_DIR}"
LOCAL_BIFRAME_COMPILER="${TOOLS_DIR}/biframe-sysc"
cp "${BIFRAME_COMPILER}" "${LOCAL_BIFRAME_COMPILER}"
chmod +x "${LOCAL_BIFRAME_COMPILER}"

RUNTIME_COMPARE_DIR="${ROOT_DIR}/tests/.out/runtime"
if [[ ! -d "${RUNTIME_COMPARE_DIR}" ]]; then
  mkdir -p "${RUNTIME_COMPARE_DIR}" || true
fi
if [[ ! -w "${RUNTIME_COMPARE_DIR}" ]]; then
  RUNTIME_COMPARE_DIR="${ROOT_DIR}/.runtime-reports/runtime"
  mkdir -p "${RUNTIME_COMPARE_DIR}"
fi

sisy_csv="${RUNTIME_COMPARE_DIR}/sisyphus-${SUITE}-${TARGET}-${OPT}.csv"
biframe_csv="${RUNTIME_COMPARE_DIR}/biframe-${SUITE}-${TARGET}-${OPT}.csv"

VS_DIR="${ROOT_DIR}/tests/.out/runtime"
if [[ ! -d "${VS_DIR}" ]]; then
  mkdir -p "${VS_DIR}" || true
fi
if [[ ! -w "${VS_DIR}" ]]; then
  VS_DIR="${ROOT_DIR}/.runtime-reports"
  mkdir -p "${VS_DIR}"
fi
vs_csv="${VS_DIR}/vs-biframe-${SUITE}-${TARGET}-${OPT}.csv"

set +e
RUNTIME_LABEL=sisyphus SISY_COMPILER_PATH="${ROOT_DIR}/build/compiler" SISY_COMPILER_FLAVOR=sisy RUNTIME_CSV="${sisy_csv}" RUNTIME_SOFT_PERF=1 "${EVAL_RUNTIME}" "${SUITE}" "${TARGET}" "${OPT}"
sisy_rc=$?
RUNTIME_LABEL=biframe SISY_COMPILER_PATH="${LOCAL_BIFRAME_COMPILER}" SISY_COMPILER_FLAVOR=biframe RUNTIME_CSV="${biframe_csv}" RUNTIME_SOFT_PERF=1 "${EVAL_RUNTIME}" "${SUITE}" "${TARGET}" "${OPT}"
biframe_rc=$?
set -e

if [[ ! -f "${sisy_csv}" || ! -f "${biframe_csv}" ]]; then
  echo "error: runtime csv missing"
  exit 1
fi

declare -A s_ms s_pass s_status
declare -A b_ms b_pass b_status

while IFS=, read -r suite case_id target opt label status compare pass median_ms warmup_ms run1_ms run2_ms run3_ms asm exe log; do
  [[ "${suite}" == "suite" ]] && continue
  s_ms["${case_id}"]="${median_ms}"
  s_pass["${case_id}"]="${pass}"
  s_status["${case_id}"]="${status}"
done <"${sisy_csv}"

while IFS=, read -r suite case_id target opt label status compare pass median_ms warmup_ms run1_ms run2_ms run3_ms asm exe log; do
  [[ "${suite}" == "suite" ]] && continue
  b_ms["${case_id}"]="${median_ms}"
  b_pass["${case_id}"]="${pass}"
  b_status["${case_id}"]="${status}"
done <"${biframe_csv}"

mkdir -p "$(dirname "${vs_csv}")"
printf 'case_id,sisyphus_ms,biframe_ms,ratio,sisyphus_status,biframe_status\n' >"${vs_csv}"

total=0
passed=0
timeout_count=0
compile_fail_count=0
ratios=()

for case_id in "${!s_ms[@]}"; do
  total=$((total + 1))
  s_status_cur="${s_status[${case_id}]}"
  b_status_cur="${b_status[${case_id}]:-missing}"
  s_pass_cur="${s_pass[${case_id}]:-0}"
  b_pass_cur="${b_pass[${case_id}]:-0}"

  if [[ "${s_status_cur}" == "timeout" ]]; then
    timeout_count=$((timeout_count + 1))
  fi
  if [[ "${s_status_cur}" == "compile_fail" || "${s_status_cur}" == "link_fail" ]]; then
    compile_fail_count=$((compile_fail_count + 1))
  fi

  ratio=""
  if [[ "${s_pass_cur}" == "1" && "${b_pass_cur}" == "1" && -n "${s_ms[${case_id}]}" && -n "${b_ms[${case_id}]}" ]]; then
    ratio="$(awk -v a="${s_ms[${case_id}]}" -v b="${b_ms[${case_id}]}" 'BEGIN { if (b <= 0) print ""; else printf "%.6f", a / b }')"
    if [[ -n "${ratio}" ]]; then
      ratios+=("${ratio}")
      passed=$((passed + 1))
    fi
  fi

  printf '%s,%s,%s,%s,%s,%s\n' \
    "${case_id}" "${s_ms[${case_id}]:-}" "${b_ms[${case_id}]:-}" "${ratio}" \
    "${s_status_cur}" "${b_status_cur}" >>"${vs_csv}"
done

pass_rate="$(awk -v p="${passed}" -v t="${total}" 'BEGIN { if (t == 0) printf "0.000000"; else printf "%.6f", p / t }')"
median_ratio=""
p90_ratio=""
if [[ "${#ratios[@]}" -gt 0 ]]; then
  median_ratio="$(printf '%s\n' "${ratios[@]}" | sort -n | awk '{a[NR]=$1} END { if (NR % 2 == 1) printf "%.6f", a[(NR+1)/2]; else printf "%.6f", (a[NR/2] + a[NR/2+1]) / 2.0 }')"
  p90_ratio="$(printf '%s\n' "${ratios[@]}" | sort -n | awk '{a[NR]=$1} END { idx=int((NR*9+9)/10); if (idx < 1) idx=1; if (idx > NR) idx=NR; printf "%.6f", a[idx] }')"
fi

if [[ "${TARGET}" == "riscv" ]]; then
  stageA_median=1.15
  stageA_p90=1.30
  stageB_median=1.05
else
  stageA_median=1.25
  stageA_p90=1.40
  stageB_median=1.10
fi

stageA="FAIL"
stageB="FAIL"
if [[ -n "${median_ratio}" && -n "${p90_ratio}" ]]; then
  if awk -v m="${median_ratio}" -v p="${p90_ratio}" -v mm="${stageA_median}" -v pp="${stageA_p90}" 'BEGIN { exit !((m <= mm) && (p <= pp)) }'; then
    stageA="PASS"
  fi
  if awk -v m="${median_ratio}" -v mm="${stageB_median}" 'BEGIN { exit !(m <= mm) }'; then
    stageB="PASS"
  fi
fi

echo "vs csv: ${vs_csv}"
echo "summary:"
echo "  sisy_rc=${sisy_rc}, biframe_rc=${biframe_rc}"
echo "  total_cases=${total}"
echo "  pass_rate=${pass_rate}"
echo "  median_ratio=${median_ratio:-N/A}"
echo "  p90_ratio=${p90_ratio:-N/A}"
echo "  timeout_count=${timeout_count}"
echo "  compile_fail_count=${compile_fail_count}"
echo "  stageA=${stageA} (median<=${stageA_median}, p90<=${stageA_p90})"
echo "  stageB=${stageB} (median<=${stageB_median})"

if [[ "${sisy_rc}" -ne 0 || "${biframe_rc}" -ne 0 ]]; then
  exit 1
fi
