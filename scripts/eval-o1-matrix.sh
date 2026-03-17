#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <case_dir>"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CASE_DIR="$1"
COMPILER="${ROOT_DIR}/build/compiler"

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

calc_metrics() {
  local case_dir="$1"
  local o0_dir="$2"
  local o1_dir="$3"
  local regressed=0
  local positive_sum=0
  local trio_sum=0
  local break_delta=0
  local fib_delta=0
  local ltcmp_delta=0

  while IFS= read -r -d '' f; do
    local rel stem base s0 s1
    rel="${f#${case_dir}/}"
    stem="${rel//\//__}"
    stem="${stem// /_}"
    stem="${stem%.*}"
    base="$(basename "${rel%.*}")"
    s0="${o0_dir}/${stem}.s"
    s1="${o1_dir}/${stem}.s"
    [[ -f "${s0}" && -f "${s1}" ]] || continue

    local l0 l1 delta
    l0="$(wc -l <"${s0}")"
    l1="$(wc -l <"${s1}")"
    delta=$((l1 - l0))

    if (( delta > 0 )); then
      regressed=$((regressed + 1))
      positive_sum=$((positive_sum + delta))
      case "${base}" in
      break)
        break_delta="${delta}"
        trio_sum=$((trio_sum + delta))
        ;;
      fib)
        fib_delta="${delta}"
        trio_sum=$((trio_sum + delta))
        ;;
      ltcmp)
        ltcmp_delta="${delta}"
        trio_sum=$((trio_sum + delta))
        ;;
      esac
    fi
  done < <(find "${case_dir}" -type f \( -name "*.sy" -o -name "*.c" \) -print0 | sort -z)

  echo "${regressed} ${positive_sum} ${trio_sum} ${break_delta} ${fib_delta} ${ltcmp_delta}"
}

configs=(
  "C0 200 200 on on"
  "C1 160 160 on on"
  "C2 128 128 on on"
  "C3 200 200 off on"
  "C4 200 200 on off"
  "C5 160 160 off on"
  "C6 160 160 on off"
  "C7 160 160 off off"
)

OUT_BASE_TAG="matrix-base"
OUT_TAG="${OUT_BASE_TAG}" "${ROOT_DIR}/scripts/regression.sh" "${CASE_DIR}" riscv O0

results="$(mktemp)"
trap 'rm -f "${results}"' EXIT
echo -e "regressed\tpositive_sum\ttrio_sum\toff_count\tconfig\tinline\tlate\trotate\tunroll\tbreak_delta\tfib_delta\tltcmp_delta" >"${results}"

for line in "${configs[@]}"; do
  read -r id inline late rotate unroll <<<"${line}"
  tag="matrix-${id}"
  extra=( "--inline-threshold=${inline}" "--late-inline-threshold=${late}" )
  off_count=0

  if [[ "${rotate}" == "off" ]]; then
    extra+=( "--disable-loop-rotate" )
    off_count=$((off_count + 1))
  else
    extra+=( "--enable-loop-rotate" )
  fi
  if [[ "${unroll}" == "off" ]]; then
    extra+=( "--disable-const-unroll" )
    off_count=$((off_count + 1))
  fi

  echo "[matrix] ${id} inline=${inline} late=${late} rotate=${rotate} unroll=${unroll}"

  OUT_TAG="${tag}" "${ROOT_DIR}/scripts/regression.sh" "${CASE_DIR}" riscv O1 "${extra[@]}"
  OUT_TAG="${tag}" "${ROOT_DIR}/scripts/regression.sh" "${CASE_DIR}" arm O1 "${extra[@]}"
  OUT_TAG="${tag}" "${ROOT_DIR}/scripts/compare.sh" "${CASE_DIR}" riscv O1 "${extra[@]}"
  OUT_TAG="${tag}" "${ROOT_DIR}/scripts/compare.sh" "${CASE_DIR}" arm O1 "${extra[@]}"

  o0_dir="${ROOT_DIR}/tests/.out/riscv-O0-${OUT_BASE_TAG}"
  o1_dir="${ROOT_DIR}/tests/.out/riscv-O1-${tag}"
  read -r regressed positive_sum trio_sum break_delta fib_delta ltcmp_delta \
    <<<"$(calc_metrics "${CASE_DIR}" "${o0_dir}" "${o1_dir}")"

  echo -e "${regressed}\t${positive_sum}\t${trio_sum}\t${off_count}\t${id}\t${inline}\t${late}\t${rotate}\t${unroll}\t${break_delta}\t${fib_delta}\t${ltcmp_delta}" >>"${results}"
done

echo
echo "=== Ranked Results (by regressed, positive_sum, trio_sum, off_count) ==="
{
  head -n1 "${results}"
  tail -n +2 "${results}" | sort -t$'\t' -k1,1n -k2,2n -k3,3n -k4,4n
} | awk -F'\t' '{
  if (NR == 1) {
    printf "%-4s %-3s %-4s %-4s %-3s %-6s %-6s %-9s %-9s %-6s %-4s %-6s\n", "cfg", "reg", "sum+", "trio", "off", "inline", "late", "rotate", "unroll", "break", "fib", "ltcmp";
    next;
  }
  printf "%-4s %-3s %-4s %-4s %-3s %-6s %-6s %-9s %-9s %-6s %-4s %-6s\n", $5, $1, $2, $3, $4, $6, $7, $8, $9, $10, $11, $12;
}'

best="$(tail -n +2 "${results}" | sort -t$'\t' -k1,1n -k2,2n -k3,3n -k4,4n | head -n1)"
read -r regressed positive_sum trio_sum off_count id inline late rotate unroll break_delta fib_delta ltcmp_delta <<<"${best}"

echo
echo "BEST: ${id} inline=${inline} late=${late} rotate=${rotate} unroll=${unroll}"

if (( regressed <= 1 && break_delta <= 1 && fib_delta <= 1 && ltcmp_delta <= 1 )); then
  echo "THRESHOLD: PASS (Regressed<=1 and break/fib/ltcmp <= +1)"
else
  echo "THRESHOLD: FAIL (keep default 200/200 with rotate off and unroll on)"
fi
