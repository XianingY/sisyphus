#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <case_dir> [riscv|arm] [O0|O1|O2] [extra compiler args...]"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${ROOT_DIR}/build/compiler"
CASE_DIR="$(realpath -m "$1")"
TARGET="${2:-riscv}"
OPT="${3:-O1}"
EXTRA_ARGS=("${@:4}")
if [[ -n "${SISY_COMPILER_EXTRA_ARGS:-}" ]]; then
  # shellcheck disable=SC2206
  EXTRA_ARGS+=(${SISY_COMPILER_EXTRA_ARGS})
fi
TAG="${OUT_TAG:-}"
COMPARE_TIMEOUT_SEC="${COMPARE_TIMEOUT_SEC:-120}"
COMPARE_INCLUDE_PERF="${COMPARE_INCLUDE_PERF:-0}"
OUT_DIR="${ROOT_DIR}/tests/.out/compare-${TARGET}-${OPT}"
if [[ -n "${TAG}" ]]; then
  OUT_DIR="${OUT_DIR}-${TAG}"
fi

mkdir -p "${OUT_DIR}"

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

if [[ ! -d "${CASE_DIR}" ]]; then
  echo "case directory not found: ${CASE_DIR}"
  exit 1
fi

safe_stem() {
  local rel="$1"
  rel="${rel//\//__}"
  rel="${rel// /_}"
  rel="${rel//$'\t'/_}"
  printf "%s" "${rel%.*}"
}

resolve_ref_out() {
  local rel="$1"
  local src="$2"
  local candidate=""

  if [[ -n "${COMPARE_REF_ROOT:-}" ]]; then
    candidate="${COMPARE_REF_ROOT}/${rel%.*}.out"
    if [[ -f "${candidate}" ]]; then
      printf "%s" "${candidate}"
      return 0
    fi
  fi

  # Legacy fallback (kept for backward compatibility).
  candidate="${ROOT_DIR}/tests/external/.refs/compiler-dev/${rel%.*}.out"
  if [[ -f "${candidate}" ]]; then
    printf "%s" "${candidate}"
    return 0
  fi

  if [[ "${src}" == *"/compiler-dev-test-cases/testcases/"* ]]; then
    local suffix="${src#*compiler-dev-test-cases/testcases/}"
    candidate="${ROOT_DIR}/tests/external/.refs/compiler-dev/${suffix%.*}.out"
    if [[ -f "${candidate}" ]]; then
      printf "%s" "${candidate}"
      return 0
    fi
  fi

  return 1
}

pass=0
fail=0
skip=0
fail_step_budget_timeout=0
fail_wall_timeout=0

while IFS= read -r -d '' f; do
  rel="${f#${CASE_DIR}/}"
  if [[ "${COMPARE_INCLUDE_PERF}" != "1" && "${rel}" == perf/* ]]; then
    skip=$((skip + 1))
    continue
  fi
  stem="$(safe_stem "${rel}")"
  base_path="${f%.*}"
  out="${base_path}.out"
  in="${base_path}.in"
  log="${OUT_DIR}/${stem}.log"

  if [[ ! -f "${out}" ]]; then
    if out_ref="$(resolve_ref_out "${rel}" "${f}")"; then
      out="${out_ref}"
    else
      echo "[skip] ${rel}: missing ${base_path}.out"
      skip=$((skip + 1))
      continue
    fi
  fi

  cmd=("${COMPILER}" "${f}" -S -o "${OUT_DIR}/${stem}.s" "--target=${TARGET}" "-${OPT}" "${EXTRA_ARGS[@]}" --compare "${out}")
  if [[ -f "${in}" ]]; then
    cmd+=(-i "${in}")
  fi

  if timeout "${COMPARE_TIMEOUT_SEC}" "${cmd[@]}" >"${log}" 2>&1; then
    pass=$((pass + 1))
  else
    status=$?
    fail=$((fail + 1))
    if [[ ${status} -eq 124 ]]; then
      fail_wall_timeout=$((fail_wall_timeout + 1))
      echo "[fail-wall-timeout] ${rel} (>${COMPARE_TIMEOUT_SEC}s, log: ${log})"
    elif grep -q "compare timed out after step budget" "${log}" 2>/dev/null; then
      fail_step_budget_timeout=$((fail_step_budget_timeout + 1))
      echo "[fail-step-budget-timeout] ${rel} (log: ${log})"
    else
      echo "[fail] ${rel} (log: ${log})"
    fi
    tail -n 20 "${log}" || true
  fi
done < <(find "${CASE_DIR}" -type f \( -name "*.sy" -o -name "*.c" \) -print0 | sort -z)

echo "Compare summary: pass=${pass}, fail=${fail}, skip=${skip}, target=${TARGET}, opt=${OPT}, compare_step_budget_timeout=${fail_step_budget_timeout}, compare_wall_timeout=${fail_wall_timeout}"
if [[ ${fail} -ne 0 ]]; then
  exit 1
fi
