#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <case_dir> [riscv|arm]"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CASE_DIR="$1"
TARGET="${2:-riscv}"
O0_DIR="${ROOT_DIR}/tests/.out/${TARGET}-O0"
O1_DIR="${ROOT_DIR}/tests/.out/${TARGET}-O1"

if [[ ! -d "${O0_DIR}" || ! -d "${O1_DIR}" ]]; then
  echo "missing ${O0_DIR} or ${O1_DIR}; run scripts/regression.sh first"
  exit 1
fi

tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT

for f in "${CASE_DIR}"/*.sy; do
  [[ -f "${f}" ]] || continue
  base="$(basename "${f}" .sy)"
  s0="${O0_DIR}/${base}.s"
  s1="${O1_DIR}/${base}.s"
  [[ -f "${s0}" && -f "${s1}" ]] || continue

  l0="$(wc -l <"${s0}")"
  l1="$(wc -l <"${s1}")"
  delta=$((l1 - l0))
  printf "%d\t%s\t%d\t%d\n" "${delta}" "${base}" "${l0}" "${l1}" >>"${tmp}"
done

echo "delta case O0 O1 (sorted by delta)"
sort -n "${tmp}" | awk '{ printf "%+d %-16s O0=%s O1=%s\n", $1, $2, $3, $4 }'

regressed="$(awk '$1 > 0 { c++ } END { print c + 0 }' "${tmp}")"
echo "Regressed cases: ${regressed}"
