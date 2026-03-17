#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_ROOT="${1:-${ROOT_DIR}/tests/.out/runtime}"
SUMMARY_LABEL="${SUMMARY_LABEL:-sisyphus}"

"${ROOT_DIR}/scripts/runtime-summary.sh" "${RUNTIME_ROOT}"

SUMMARY_DIR="${ROOT_DIR}/.runtime-reports/summary"
if [[ -d "${ROOT_DIR}/tests/.out/runtime/summary" ]]; then
  SUMMARY_DIR="${ROOT_DIR}/tests/.out/runtime/summary"
fi

status_file="${SUMMARY_DIR}/gate-status.txt"
if [[ ! -f "${status_file}" ]]; then
  echo "error: missing ${status_file}"
  exit 1
fi

echo "gate status file: ${status_file}"
cat "${status_file}"
