#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <case_dir> [riscv|arm]"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CASE_DIR="$1"
MAIN_TARGET="${2:-riscv}"
COMPILER="${ROOT_DIR}/build/compiler"

if [[ "${MAIN_TARGET}" != "riscv" && "${MAIN_TARGET}" != "arm" ]]; then
  echo "error: main target must be riscv or arm"
  exit 1
fi

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

calc_metrics() {
  local case_dir="$1"
  local o0_dir="$2"
  local ox_dir="$3"
  local regressed=0
  local positive_sum=0
  local trio_sum=0
  local break_delta=0
  local fib_delta=0
  local ltcmp_delta=0

  while IFS= read -r -d '' f; do
    local rel stem base s0 sx
    rel="${f#${case_dir}/}"
    stem="${rel//\//__}"
    stem="${stem// /_}"
    stem="${stem%.*}"
    base="$(basename "${rel%.*}")"
    s0="${o0_dir}/${stem}.s"
    sx="${ox_dir}/${stem}.s"
    [[ -f "${s0}" && -f "${sx}" ]] || continue

    local l0 l1 delta
    l0="$(wc -l <"${s0}")"
    l1="$(wc -l <"${sx}")"
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

pick_threshold() {
  local opt="$1"
  local key="$2"
  local base low1 low2
  if [[ "${opt}" == "O1" ]]; then
    base=200
    low1=160
    low2=128
  else
    base=256
    low1=224
    low2=192
  fi

  case "${key}" in
  base) echo "${base}" ;;
  low1) echo "${low1}" ;;
  low2) echo "${low2}" ;;
  *)
    echo "error: unknown threshold key ${key}" >&2
    exit 1
    ;;
  esac
}

configs=(
  "C0 base on on"
  "C1 low1 on on"
  "C2 low2 on on"
  "C3 base off on"
  "C4 base on off"
  "C5 low1 off on"
  "C6 low1 on off"
  "C7 low1 off off"
)

OUT_BASE_TAG="profile-base-${MAIN_TARGET}"
OUT_TAG="${OUT_BASE_TAG}" "${ROOT_DIR}/scripts/regression.sh" "${CASE_DIR}" "${MAIN_TARGET}" O0

results="$(mktemp)"
trap 'rm -f "${results}"' EXIT
echo -e "opt\tregressed\tpositive_sum\ttrio_sum\toff_count\tconfig\tinline\tlate\trotate\tunroll\tbreak_delta\tfib_delta\tltcmp_delta" >"${results}"

for opt in O1 O2; do
  for line in "${configs[@]}"; do
    read -r id key rotate unroll <<<"${line}"
    inline="$(pick_threshold "${opt}" "${key}")"
    late="${inline}"
    tag="matrix-${opt}-${id}"
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

    echo "[matrix] ${opt}/${id} inline=${inline} late=${late} rotate=${rotate} unroll=${unroll}"

    OUT_TAG="${tag}" "${ROOT_DIR}/scripts/regression.sh" "${CASE_DIR}" riscv "${opt}" "${extra[@]}"
    OUT_TAG="${tag}" "${ROOT_DIR}/scripts/regression.sh" "${CASE_DIR}" arm "${opt}" "${extra[@]}"
    OUT_TAG="${tag}" "${ROOT_DIR}/scripts/compare.sh" "${CASE_DIR}" riscv "${opt}" "${extra[@]}"
    OUT_TAG="${tag}" "${ROOT_DIR}/scripts/compare.sh" "${CASE_DIR}" arm "${opt}" "${extra[@]}"

    o0_dir="${ROOT_DIR}/tests/.out/${MAIN_TARGET}-O0-${OUT_BASE_TAG}"
    ox_dir="${ROOT_DIR}/tests/.out/${MAIN_TARGET}-${opt}-${tag}"
    read -r regressed positive_sum trio_sum break_delta fib_delta ltcmp_delta \
      <<<"$(calc_metrics "${CASE_DIR}" "${o0_dir}" "${ox_dir}")"

    echo -e "${opt}\t${regressed}\t${positive_sum}\t${trio_sum}\t${off_count}\t${id}\t${inline}\t${late}\t${rotate}\t${unroll}\t${break_delta}\t${fib_delta}\t${ltcmp_delta}" >>"${results}"
  done
done

echo
echo "=== Ranked Results (by regressed, positive_sum, trio_sum, off_count) ==="
{
  head -n1 "${results}"
  tail -n +2 "${results}" | sort -t$'\t' -k2,2n -k3,3n -k4,4n -k5,5n -k1,1 -k6,6
} | awk -F'\t' '{
  if (NR == 1) {
    printf "%-3s %-4s %-3s %-4s %-4s %-3s %-6s %-6s %-9s %-9s %-6s %-4s %-6s\n", "opt", "cfg", "reg", "sum+", "trio", "off", "inline", "late", "rotate", "unroll", "break", "fib", "ltcmp";
    next;
  }
  printf "%-3s %-4s %-3s %-4s %-4s %-3s %-6s %-6s %-9s %-9s %-6s %-4s %-6s\n", $1, $6, $2, $3, $4, $5, $7, $8, $9, $10, $11, $12, $13;
}'

print_best() {
  local opt="$1"
  local best
  best="$(awk -F'\t' -v opt="${opt}" '$1 == opt { print }' "${results}" | sort -t$'\t' -k2,2n -k3,3n -k4,4n -k5,5n -k6,6 | head -n1)"
  if [[ -z "${best}" ]]; then
    return 0
  fi
  read -r _opt regressed positive_sum trio_sum off_count id inline late rotate unroll break_delta fib_delta ltcmp_delta <<<"${best}"
  echo
  echo "BEST ${opt}: ${id} inline=${inline} late=${late} rotate=${rotate} unroll=${unroll}"
  if [[ "${opt}" == "O1" ]]; then
    if (( regressed <= 1 && break_delta <= 1 && fib_delta <= 1 && ltcmp_delta <= 1 )); then
      echo "O1-THRESHOLD: PASS (Regressed<=1 and break/fib/ltcmp <= +1)"
    else
      echo "O1-THRESHOLD: FAIL (keep O1 default unchanged)"
    fi
  fi
}

print_best O1
print_best O2
