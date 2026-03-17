#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXTERNAL_DIR="${ROOT_DIR}/tests/external"
OUT_DIR="${ROOT_DIR}/tests/.out/suites"
OUT_CSV="${OUT_DIR}/index.csv"
INCLUDE_SOFT=0

if [[ $# -gt 1 ]]; then
  echo "usage: $0 [--include-soft]"
  exit 1
fi
if [[ $# -eq 1 ]]; then
  if [[ "$1" == "--include-soft" ]]; then
    INCLUDE_SOFT=1
  else
    echo "error: unknown option '$1'"
    exit 1
  fi
fi

if [[ ! -d "${EXTERNAL_DIR}" ]]; then
  echo "missing ${EXTERNAL_DIR}; run scripts/suite-sync.sh first"
  exit 1
fi

mkdir -p "${OUT_DIR}"
printf 'suite,tier,kind,case_id,src,in,out,enabled\n' >"${OUT_CSV}"

abs_path() {
  realpath -m "$1"
}

emit_case() {
  local suite="$1"
  local tier="$2"
  local kind="$3"
  local case_id="$4"
  local src="$5"
  local in_file="$6"
  local out_file="$7"
  local enabled="$8"
  printf '%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "${suite}" "${tier}" "${kind}" "${case_id}" \
    "$(abs_path "${src}")" "$(abs_path "${in_file}")" "$(abs_path "${out_file}")" "${enabled}" \
    >>"${OUT_CSV}"
}

count=0

open_root="${EXTERNAL_DIR}/open-test-cases"
if [[ -d "${open_root}/sysy" ]]; then
  while IFS= read -r -d '' d; do
    while IFS= read -r -d '' src; do
      rel="${src#${d}/}"
      case_id="${rel%.*}"
      in_file="${src%.*}.in"
      out_file="${src%.*}.out"
      emit_case "open-functional" "hard" "functional" "${case_id}" "${src}" "${in_file}" "${out_file}" "1"
      count=$((count + 1))
    done < <(find "${d}" -type f -name '*.sy' -print0 | sort -z)
  done < <(find "${open_root}/sysy" -type d -name 'function_test*' -print0 | sort -z)

  while IFS= read -r -d '' d; do
    while IFS= read -r -d '' src; do
      rel="${src#${d}/}"
      case_id="${rel%.*}"
      in_file="${src%.*}.in"
      out_file="${src%.*}.out"
      emit_case "open-perf" "hard" "perf" "${case_id}" "${src}" "${in_file}" "${out_file}" "1"
      count=$((count + 1))
    done < <(find "${d}" -type f -name '*.sy' -print0 | sort -z)
  done < <(find "${open_root}/sysy" -type d -name 'performance_test*' -print0 | sort -z)
fi

dev_root="${EXTERNAL_DIR}/compiler-dev-test-cases/testcases"
if [[ -d "${dev_root}" ]]; then
  ref_root="${EXTERNAL_DIR}/.refs/compiler-dev"
  while IFS= read -r -d '' src; do
    rel="${src#${dev_root}/}"
    case_id="${rel%.*}"
    in_file="${src%.*}.in"
    out_file="${ref_root}/${case_id}.out"
    emit_case "compiler-dev" "hard" "functional" "${case_id}" "${src}" "${in_file}" "${out_file}" "1"
    count=$((count + 1))
  done < <(find "${dev_root}" -type f -name '*.c' -print0 | sort -z)
fi

lvx_root="${EXTERNAL_DIR}/sysy-testsuit-collection/lvX"
if [[ -d "${lvx_root}" ]]; then
  enabled="0"
  if [[ "${INCLUDE_SOFT}" -eq 1 ]]; then
    enabled="1"
  fi
  while IFS= read -r -d '' src; do
    rel="${src#${lvx_root}/}"
    case_id="${rel%.*}"
    in_file="${src%.*}.in"
    out_file="${src%.*}.out"
    emit_case "lvx" "soft" "functional" "${case_id}" "${src}" "${in_file}" "${out_file}" "${enabled}"
    count=$((count + 1))
  done < <(find "${lvx_root}" -type f -name '*.c' -print0 | sort -z)
fi

echo "wrote ${OUT_CSV} (${count} rows)"
