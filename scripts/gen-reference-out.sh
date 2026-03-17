#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 compiler-dev"
  exit 1
fi

SUITE="$1"
if [[ "${SUITE}" != "compiler-dev" ]]; then
  echo "error: only compiler-dev is supported"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ROOT="${ROOT_DIR}/tests/external/compiler-dev-test-cases/testcases"
REF_ROOT="${ROOT_DIR}/tests/external/.refs/compiler-dev"
LOG_ROOT="${ROOT_DIR}/tests/.out/reference/compiler-dev"
TIMEOUT_SEC="${REF_TIMEOUT_SEC:-120}"
CC="${REF_CC:-clang}"

if [[ ! -d "${SRC_ROOT}" ]]; then
  echo "missing ${SRC_ROOT}; run scripts/suite-sync.sh first"
  exit 1
fi
if ! command -v "${CC}" >/dev/null 2>&1; then
  echo "error: ${CC} not found"
  exit 1
fi

mkdir -p "${REF_ROOT}" "${LOG_ROOT}"

normalize_text() {
  local file="$1"
  awk '{ sub(/[ \t\r]+$/, "", $0); print }' "${file}"
}

count=0
fail=0
while IFS= read -r -d '' src; do
  rel="${src#${SRC_ROOT}/}"
  case_id="${rel%.*}"
  in_file="${src%.*}.in"

  out_file="${REF_ROOT}/${case_id}.out"
  log_file="${LOG_ROOT}/${case_id}.log"
  exe_file="$(mktemp "${LOG_ROOT}/run.XXXXXX")"
  stdout_file="$(mktemp "${LOG_ROOT}/stdout.XXXXXX")"
  stderr_file="$(mktemp "${LOG_ROOT}/stderr.XXXXXX")"

  mkdir -p "$(dirname "${out_file}")" "$(dirname "${log_file}")"

  {
    echo "[compile] ${rel}"
    if ! "${CC}" -std=c11 -O2 -w \
      -D'starttime()=_sysy_starttime(0)' \
      -D'stoptime()=_sysy_stoptime(0)' \
      -include "${ROOT_DIR}/runtime/sylib.h" \
      "${src}" "${ROOT_DIR}/runtime/sylib.c" -lm -o "${exe_file}"; then
      echo "compile failed"
      exit 1
    fi

    echo "[run] ${rel}"
    set +e
    if [[ -f "${in_file}" ]]; then
      timeout "${TIMEOUT_SEC}" "${exe_file}" <"${in_file}" >"${stdout_file}" 2>"${stderr_file}"
      rc=$?
    else
      timeout "${TIMEOUT_SEC}" "${exe_file}" >"${stdout_file}" 2>"${stderr_file}"
      rc=$?
    fi
    set -e

    if [[ "${rc}" -eq 124 ]]; then
      echo "timeout (${TIMEOUT_SEC}s)"
      exit 124
    fi

    normalize_text "${stdout_file}" >"${out_file}"
    printf '%d\n' "${rc}" >>"${out_file}"
  } >"${log_file}" 2>&1 || {
    fail=$((fail + 1))
    echo "[fail] ${rel} (log: ${log_file})"
  }

  rm -f "${exe_file}" "${stdout_file}" "${stderr_file}"
  count=$((count + 1))
done < <(find "${SRC_ROOT}" -type f -name '*.c' -print0 | sort -z)

echo "generated references: total=${count}, fail=${fail}, root=${REF_ROOT}"
if [[ "${fail}" -ne 0 ]]; then
  exit 1
fi
