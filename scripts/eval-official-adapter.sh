#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${ROOT_DIR}/build/compiler"

FUNCTIONAL_DIR="${1:-${OFFICIAL_FUNCTIONAL_DIR:-}}"
PERF_DIR="${2:-${OFFICIAL_PERF_DIR:-}}"
RUNTIME_DIR="${3:-${OFFICIAL_RUNTIME_DIR:-}}"
CSV_OUT="${OFFICIAL_CSV_OUT:-${ROOT_DIR}/tests/.out/official/eval.csv}"

read -r -a TARGETS <<<"${OFFICIAL_TARGETS:-riscv arm}"
read -r -a OPTS <<<"${OFFICIAL_OPTS:-O0 O1 O2}"

if [[ -z "${FUNCTIONAL_DIR}" && -z "${PERF_DIR}" ]]; then
  echo "skip: no official directories provided."
  echo "usage: $0 [functional_dir] [perf_dir] [runtime_dir]"
  echo "hint: you can also set OFFICIAL_FUNCTIONAL_DIR / OFFICIAL_PERF_DIR."
  exit 0
fi

if [[ ! -x "${COMPILER}" ]]; then
  echo "compiler not found at ${COMPILER}; run scripts/build.sh first"
  exit 1
fi

if [[ -n "${RUNTIME_DIR}" && ! -d "${RUNTIME_DIR}" ]]; then
  echo "warning: runtime dir does not exist: ${RUNTIME_DIR}"
fi

mkdir -p "$(dirname "${CSV_OUT}")"
printf "suite,case,target,opt,status,compare,asm_lines,log\n" >"${CSV_OUT}"

escape_csv() {
  local s="$1"
  s="${s//\"/\"\"}"
  printf '"%s"' "${s}"
}

fail=0
total=0

run_suite() {
  local suite="$1"
  local dir="$2"
  local compare_mode="$3"
  if [[ -z "${dir}" ]]; then
    return 0
  fi
  if [[ ! -d "${dir}" ]]; then
    echo "[skip] ${suite}: directory not found: ${dir}"
    return 0
  fi

  mapfile -t cases < <(find "${dir}" -type f -name "*.sy" | sort)
  if [[ "${#cases[@]}" -eq 0 ]]; then
    echo "[skip] ${suite}: no .sy files under ${dir}"
    return 0
  fi

  echo "[suite] ${suite}: ${#cases[@]} cases"
  for f in "${cases[@]}"; do
    local rel case_id out_file in_file safe
    rel="${f#${dir}/}"
    case_id="${rel%.sy}"
    out_file="${f%.sy}.out"
    in_file="${f%.sy}.in"
    safe="${case_id//\//__}"

    for target in "${TARGETS[@]}"; do
      for opt in "${OPTS[@]}"; do
        total=$((total + 1))
        local asm_dir asm_path log_path status compare_status lines
        asm_dir="${ROOT_DIR}/tests/.out/official/asm/${suite}/${target}-${opt}"
        asm_path="${asm_dir}/${safe}.s"
        log_path="${ROOT_DIR}/tests/.out/official/logs/${suite}/${target}-${opt}/${safe}.log"
        mkdir -p "${asm_dir}" "$(dirname "${log_path}")"

        status="ok"
        compare_status="skip"
        lines=0
        cmd=( "${COMPILER}" "${f}" -S -o "${asm_path}" "--target=${target}" "-${opt}" )

        if [[ "${compare_mode}" == "yes" && -f "${out_file}" ]]; then
          cmd+=( --compare "${out_file}" )
          compare_status="ok"
          if [[ -f "${in_file}" ]]; then
            cmd+=( -i "${in_file}" )
          fi
        elif [[ "${compare_mode}" == "yes" ]]; then
          compare_status="no_out"
        fi

        if "${cmd[@]}" >"${log_path}" 2>&1; then
          if [[ -f "${asm_path}" ]]; then
            lines="$(wc -l <"${asm_path}")"
          fi
        else
          status="fail"
          if [[ "${compare_status}" == "ok" ]]; then
            compare_status="fail"
          fi
          fail=$((fail + 1))
        fi

        printf "%s,%s,%s,%s,%s,%s,%s,%s\n" \
          "${suite}" \
          "$(escape_csv "${case_id}")" \
          "${target}" \
          "${opt}" \
          "${status}" \
          "${compare_status}" \
          "${lines}" \
          "$(escape_csv "${log_path}")" >>"${CSV_OUT}"
      done
    done
  done
}

run_suite functional "${FUNCTIONAL_DIR}" yes
run_suite perf "${PERF_DIR}" no

echo "CSV written to ${CSV_OUT}"
echo "Summary: total=${total}, fail=${fail}"
if [[ -n "${RUNTIME_DIR}" ]]; then
  echo "runtime dir: ${RUNTIME_DIR}"
fi

if [[ "${fail}" -ne 0 ]]; then
  exit 1
fi
