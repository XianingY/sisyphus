#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <case_dir> [riscv|arm]"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CASE_DIR="$(realpath -m "$1")"
TARGET="${2:-riscv}"
O0_DIR="${ROOT_DIR}/tests/.out/${TARGET}-O0"
O1_DIR="${ROOT_DIR}/tests/.out/${TARGET}-O1"

if [[ ! -d "${O0_DIR}" || ! -d "${O1_DIR}" ]]; then
  echo "missing ${O0_DIR} or ${O1_DIR}; run scripts/regression.sh first"
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

tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT

while IFS= read -r -d '' f; do
  rel="${f#${CASE_DIR}/}"
  stem="$(safe_stem "${rel}")"
  s0="${O0_DIR}/${stem}.s"
  s1="${O1_DIR}/${stem}.s"
  [[ -f "${s0}" && -f "${s1}" ]] || continue

  l0="$(wc -l <"${s0}")"
  l1="$(wc -l <"${s1}")"
  delta=$((l1 - l0))
  printf "%d\t%s\t%d\t%d\n" "${delta}" "${rel%.*}" "${l0}" "${l1}" >>"${tmp}"
done < <(find "${CASE_DIR}" -type f \( -name "*.sy" -o -name "*.c" \) -print0 | sort -z)

echo "delta case O0 O1 (sorted by delta)"
sort -n "${tmp}" | awk '{ printf "%+d %-40s O0=%s O1=%s\n", $1, $2, $3, $4 }'

regressed="$(awk '$1 > 0 { c++ } END { print c + 0 }' "${tmp}")"
echo "Regressed cases: ${regressed}"
