#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <suite> <target> <opt>"
  echo "suite: open-functional | open-perf | compiler-dev | lvx"
  echo "target: riscv | arm"
  echo "opt: O0 | O1 | O2"
  exit 1
fi

SUITE="$1"
TARGET="$2"
OPT="$3"

if [[ "${SUITE}" != "open-functional" && "${SUITE}" != "open-perf" && "${SUITE}" != "compiler-dev" && "${SUITE}" != "lvx" ]]; then
  echo "error: unsupported suite '${SUITE}'"
  exit 1
fi
if [[ "${TARGET}" != "riscv" && "${TARGET}" != "arm" ]]; then
  echo "error: target must be riscv|arm"
  exit 1
fi
if [[ "${OPT}" != "O0" && "${OPT}" != "O1" && "${OPT}" != "O2" ]]; then
  echo "error: opt must be O0|O1|O2"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INDEX_SCRIPT="${ROOT_DIR}/scripts/suite-index.sh"
DOCKERFILE="${ROOT_DIR}/docker/compiler-dev-dual.Dockerfile"
COMPILER_PATH="${SISY_COMPILER_PATH:-${ROOT_DIR}/build/compiler}"
COMPILER_FLAVOR="${SISY_COMPILER_FLAVOR:-sisy}"
LABEL="${RUNTIME_LABEL:-sisyphus}"
RUNTIME_TIMEOUT_SEC="${RUNTIME_TIMEOUT_SEC:-10}"
RUNTIME_PERF_TIMEOUT_SEC="${RUNTIME_PERF_TIMEOUT_SEC:-${RUNTIME_TIMEOUT_SEC}}"
SISY_DOCKER_IMAGE="${SISY_DOCKER_IMAGE:-sisyphus/compiler-dev-dual:latest}"
DEFAULT_RUNTIME_ROOT="${ROOT_DIR}/tests/.out/runtime"
RUNTIME_ROOT="${RUNTIME_ROOT:-${DEFAULT_RUNTIME_ROOT}}"
CSV_EXPLICIT=0
if [[ -n "${RUNTIME_CSV:-}" ]]; then
  CSV_EXPLICIT=1
fi
CSV_OUT="${RUNTIME_CSV:-${RUNTIME_ROOT}/${LABEL}-${SUITE}-${TARGET}-${OPT}.csv}"
RUNTIME_CASE_LIMIT="${RUNTIME_CASE_LIMIT:-0}"
RUNTIME_CASE_FILTER="${RUNTIME_CASE_FILTER:-}"
RUNTIME_SOFT_PERF="${RUNTIME_SOFT_PERF:-0}"

if [[ "${COMPILER_FLAVOR}" != "sisy" && "${COMPILER_FLAVOR}" != "biframe" ]]; then
  echo "error: SISY_COMPILER_FLAVOR must be sisy|biframe"
  exit 1
fi

if [[ "${SISY_RUNTIME_IN_DOCKER:-0}" != "1" && "${SISY_RUNTIME_LOCAL:-0}" != "1" ]]; then
  if command -v docker >/dev/null 2>&1; then
    if [[ ! -f "${DOCKERFILE}" ]]; then
      echo "error: missing Dockerfile ${DOCKERFILE}"
      exit 1
    fi

    if ! docker image inspect "${SISY_DOCKER_IMAGE}" >/dev/null 2>&1; then
      echo "[docker] build ${SISY_DOCKER_IMAGE}"
      docker build -t "${SISY_DOCKER_IMAGE}" -f "${DOCKERFILE}" "${ROOT_DIR}"
    fi

    echo "[docker] run ${SUITE} ${TARGET} ${OPT}"
    docker run --rm \
      --user "$(id -u):$(id -g)" \
      -e SISY_RUNTIME_IN_DOCKER=1 \
      -e SISY_RUNTIME_LOCAL=1 \
      -e SISY_COMPILER_PATH="${COMPILER_PATH}" \
      -e SISY_COMPILER_FLAVOR="${COMPILER_FLAVOR}" \
      -e RUNTIME_LABEL="${LABEL}" \
      -e RUNTIME_TIMEOUT_SEC="${RUNTIME_TIMEOUT_SEC}" \
      -e RUNTIME_PERF_TIMEOUT_SEC="${RUNTIME_PERF_TIMEOUT_SEC}" \
      -e RUNTIME_SOFT_PERF="${RUNTIME_SOFT_PERF}" \
      -e RUNTIME_CSV="${CSV_OUT}" \
      -e RUNTIME_CASE_LIMIT="${RUNTIME_CASE_LIMIT}" \
      -e RUNTIME_CASE_FILTER="${RUNTIME_CASE_FILTER}" \
      -v "${ROOT_DIR}:${ROOT_DIR}" \
      -w "${ROOT_DIR}" \
      "${SISY_DOCKER_IMAGE}" \
      bash -lc "scripts/eval-runtime.sh '${SUITE}' '${TARGET}' '${OPT}'"
    exit $?
  fi
fi

if [[ ! -x "${COMPILER_PATH}" ]]; then
  echo "error: compiler not found at ${COMPILER_PATH}"
  exit 1
fi
if [[ ! -x "${INDEX_SCRIPT}" ]]; then
  echo "error: missing ${INDEX_SCRIPT}"
  exit 1
fi

if [[ "${SUITE}" == "lvx" ]]; then
  "${INDEX_SCRIPT}" --include-soft
else
  "${INDEX_SCRIPT}"
fi

INDEX_CSV="${ROOT_DIR}/tests/.out/suites/index.csv"
if [[ ! -f "${INDEX_CSV}" ]]; then
  echo "error: index missing at ${INDEX_CSV}"
  exit 1
fi

if [[ "${TARGET}" == "riscv" ]]; then
  GCC_BIN="riscv64-linux-gnu-gcc"
  QEMU_BIN="qemu-riscv64-static"
else
  GCC_BIN="aarch64-linux-gnu-gcc"
  QEMU_BIN="qemu-aarch64-static"
fi

for tool in "${GCC_BIN}" "${QEMU_BIN}" timeout awk sort; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "error: missing tool '${tool}'."
    echo "hint: install toolchain locally or run with Docker enabled."
    exit 1
  fi
done

if ! mkdir -p "${RUNTIME_ROOT}" 2>/dev/null || [[ ! -w "${RUNTIME_ROOT}" ]]; then
  RUNTIME_ROOT="${ROOT_DIR}/.runtime-reports/runtime"
  mkdir -p "${RUNTIME_ROOT}"
  if [[ "${CSV_EXPLICIT}" -eq 0 ]]; then
    CSV_OUT="${RUNTIME_ROOT}/${LABEL}-${SUITE}-${TARGET}-${OPT}.csv"
  fi
fi

csv_dir="$(dirname "${CSV_OUT}")"
if ! mkdir -p "${csv_dir}" 2>/dev/null || [[ ! -w "${csv_dir}" ]]; then
  csv_dir="${RUNTIME_ROOT}"
  mkdir -p "${csv_dir}"
  CSV_OUT="${csv_dir}/$(basename "${CSV_OUT}")"
fi

safe_stem() {
  local id="$1"
  local stem
  stem="${id//\//__}"
  stem="${stem// /_}"
  stem="${stem//$'\t'/_}"
  printf "%s" "${stem}"
}

ns_to_ms() {
  local ns="$1"
  awk -v ns="${ns}" 'BEGIN { printf "%.3f", ns / 1000000.0 }'
}

normalize_text() {
  local file="$1"
  awk '{ sub(/[ \t\r]+$/, "", $0); print }' "${file}"
}

run_once() {
  local exe="$1"
  local in_file="$2"
  local stdout_file="$3"
  local stderr_file="$4"
  local timeout_sec="$5"

  local start end rc
  start="$(date +%s%N)"
  set +e
  if [[ -f "${in_file}" ]]; then
    timeout "${timeout_sec}" "${QEMU_BIN}" "${exe}" <"${in_file}" >"${stdout_file}" 2>"${stderr_file}"
    rc=$?
  else
    timeout "${timeout_sec}" "${QEMU_BIN}" "${exe}" >"${stdout_file}" 2>"${stderr_file}"
    rc=$?
  fi
  set -e
  end="$(date +%s%N)"

  RUN_RC="${rc}"
  RUN_NS=$((end - start))
}

compile_case() {
  local src="$1"
  local asm="$2"

  if [[ "${COMPILER_FLAVOR}" == "sisy" ]]; then
    "${COMPILER_PATH}" "${src}" -S -o "${asm}" "--target=${TARGET}" "-${OPT}"
    return 0
  fi

  # biframe compatibility mode.
  local biframe_opt=""
  if [[ "${OPT}" != "O0" ]]; then
    biframe_opt="-O1"
  fi

  if [[ "${TARGET}" == "arm" ]]; then
    if [[ -n "${biframe_opt}" ]]; then
      "${COMPILER_PATH}" "${src}" -S -o "${asm}" --arm "${biframe_opt}"
    else
      "${COMPILER_PATH}" "${src}" -S -o "${asm}" --arm
    fi
  else
    if [[ -n "${biframe_opt}" ]]; then
      "${COMPILER_PATH}" "${src}" -S -o "${asm}" "${biframe_opt}"
    else
      "${COMPILER_PATH}" "${src}" -S -o "${asm}"
    fi
  fi
}

printf 'suite,case_id,target,opt,label,status,compare,pass,median_ms,warmup_ms,run1_ms,run2_ms,run3_ms,asm,exe,log\n' >"${CSV_OUT}"

asm_root="${RUNTIME_ROOT}/asm/${SUITE}/${LABEL}/${TARGET}-${OPT}"
bin_root="${RUNTIME_ROOT}/bin/${SUITE}/${LABEL}/${TARGET}-${OPT}"
log_root="${RUNTIME_ROOT}/logs/${SUITE}/${LABEL}/${TARGET}-${OPT}"
mkdir -p "${asm_root}" "${bin_root}" "${log_root}"

total=0
pass_count=0
fail_count=0
hard_fail_count=0
soft_fail_count=0

while IFS=, read -r suite tier kind case_id src in_file out_file enabled; do
  [[ "${suite}" == "suite" ]] && continue
  [[ "${suite}" == "${SUITE}" ]] || continue
  [[ "${enabled}" == "1" ]] || continue
  if [[ -n "${RUNTIME_CASE_FILTER}" ]] && [[ "${case_id}" != *"${RUNTIME_CASE_FILTER}"* ]]; then
    continue
  fi
  if [[ "${RUNTIME_CASE_LIMIT}" -gt 0 && "${total}" -ge "${RUNTIME_CASE_LIMIT}" ]]; then
    break
  fi

  total=$((total + 1))

  stem="$(safe_stem "${case_id}")"
  asm_path="${asm_root}/${stem}.s"
  exe_path="${bin_root}/${stem}"
  log_path="${log_root}/${stem}.log"
  rm -f "${asm_path}" "${exe_path}"

  status="ok"
  compare_status="skip"
  pass=0
  warmup_ms=""
  run1_ms=""
  run2_ms=""
  run3_ms=""
  median_ms=""

  tmp_dir="$(mktemp -d "${log_root}/tmp.XXXXXX")"
  stdout_warm="${tmp_dir}/warm.out"
  stderr_warm="${tmp_dir}/warm.err"
  stdout_1="${tmp_dir}/run1.out"
  stderr_1="${tmp_dir}/run1.err"
  stdout_2="${tmp_dir}/run2.out"
  stderr_2="${tmp_dir}/run2.err"
  stdout_3="${tmp_dir}/run3.out"
  stderr_3="${tmp_dir}/run3.err"
  actual_file="${tmp_dir}/actual.out"
  timeout_sec="${RUNTIME_TIMEOUT_SEC}"
  if [[ "${case_id}" == perf/* ]]; then
    timeout_sec="${RUNTIME_PERF_TIMEOUT_SEC}"
  fi

  echo "[case] ${case_id}" >"${log_path}"
  echo "[timeout] ${timeout_sec}s" >>"${log_path}"
  echo "[compile] ${src}" >>"${log_path}"
  if ! compile_case "${src}" "${asm_path}" >>"${log_path}" 2>&1; then
    status="compile_fail"
  fi

  if [[ "${status}" == "ok" ]]; then
    echo "[link] ${asm_path}" >>"${log_path}"
    if ! "${GCC_BIN}" -static "${asm_path}" "${ROOT_DIR}/runtime/sylib.c" -lm -o "${exe_path}" >>"${log_path}" 2>&1; then
      status="link_fail"
    fi
  fi

  if [[ "${status}" == "ok" ]]; then
    echo "[warmup]" >>"${log_path}"
    run_once "${exe_path}" "${in_file}" "${stdout_warm}" "${stderr_warm}" "${timeout_sec}"
    if [[ "${RUN_RC}" -eq 124 ]]; then
      status="timeout"
      echo "timeout in warmup" >>"${log_path}"
    else
      warmup_ms="$(ns_to_ms "${RUN_NS}")"
    fi
  fi

  if [[ "${status}" == "ok" ]]; then
    echo "[run] 1" >>"${log_path}"
    run_once "${exe_path}" "${in_file}" "${stdout_1}" "${stderr_1}" "${timeout_sec}"
    if [[ "${RUN_RC}" -eq 124 ]]; then
      status="timeout"
      echo "timeout in run1" >>"${log_path}"
    else
      rc1="${RUN_RC}"
      ns1="${RUN_NS}"
      run1_ms="$(ns_to_ms "${ns1}")"
    fi
  fi

  if [[ "${status}" == "ok" ]]; then
    echo "[run] 2" >>"${log_path}"
    run_once "${exe_path}" "${in_file}" "${stdout_2}" "${stderr_2}" "${timeout_sec}"
    if [[ "${RUN_RC}" -eq 124 ]]; then
      status="timeout"
      echo "timeout in run2" >>"${log_path}"
    else
      ns2="${RUN_NS}"
      run2_ms="$(ns_to_ms "${ns2}")"
    fi
  fi

  if [[ "${status}" == "ok" ]]; then
    echo "[run] 3" >>"${log_path}"
    run_once "${exe_path}" "${in_file}" "${stdout_3}" "${stderr_3}" "${timeout_sec}"
    if [[ "${RUN_RC}" -eq 124 ]]; then
      status="timeout"
      echo "timeout in run3" >>"${log_path}"
    else
      ns3="${RUN_NS}"
      run3_ms="$(ns_to_ms "${ns3}")"

      median_ns="$(printf '%s\n' "${ns1}" "${ns2}" "${ns3}" | sort -n | awk 'NR==2 { print; exit }')"
      median_ms="$(ns_to_ms "${median_ns}")"

      cp "${stdout_1}" "${actual_file}"
      if [[ -s "${actual_file}" ]]; then
        # Keep exactly one separator line between stdout and exit code.
        last_byte="$(tail -c 1 "${actual_file}" | od -An -t x1 | tr -d '[:space:]')"
        if [[ "${last_byte}" != "0a" ]]; then
          echo >>"${actual_file}"
        fi
      fi
      printf '%d\n' "${rc1}" >>"${actual_file}"

      if [[ -f "${out_file}" ]]; then
        compare_status="ok"
        expected_norm="$(normalize_text "${out_file}")"
        actual_norm="$(normalize_text "${actual_file}")"
        if [[ "${expected_norm}" != "${actual_norm}" ]]; then
          compare_status="fail"
          status="mismatch"
          {
            echo "[mismatch] exit_code(run1)=${rc1}"
            echo "[mismatch] expected(norm):"
            normalize_text "${out_file}"
            echo "[mismatch] actual(norm):"
            normalize_text "${actual_file}"
          } >>"${log_path}"
        fi
      else
        compare_status="no_out"
      fi
    fi
  fi

  if [[ "${status}" != "ok" && "${status}" != "mismatch" && "${status}" != "timeout" ]]; then
    compare_status="skip"
  fi

  if [[ "${status}" == "ok" && ( "${compare_status}" == "ok" || "${compare_status}" == "no_out" ) ]]; then
    pass=1
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
    if [[ "${RUNTIME_SOFT_PERF}" == "1" && "${case_id}" == perf/* ]]; then
      soft_fail_count=$((soft_fail_count + 1))
    else
      hard_fail_count=$((hard_fail_count + 1))
    fi
  fi

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "${SUITE}" "${case_id}" "${TARGET}" "${OPT}" "${LABEL}" \
    "${status}" "${compare_status}" "${pass}" "${median_ms}" "${warmup_ms}" \
    "${run1_ms}" "${run2_ms}" "${run3_ms}" "${asm_path}" "${exe_path}" "${log_path}" \
    >>"${CSV_OUT}"

  rm -rf "${tmp_dir}"
done <"${INDEX_CSV}"

echo "csv: ${CSV_OUT}"
echo "summary: total=${total}, pass=${pass_count}, fail=${fail_count}, hard_fail=${hard_fail_count}, soft_fail=${soft_fail_count}"
if [[ "${hard_fail_count}" -ne 0 ]]; then
  exit 1
fi
if [[ "${RUNTIME_SOFT_PERF}" != "1" && "${fail_count}" -ne 0 ]]; then
  exit 1
fi
