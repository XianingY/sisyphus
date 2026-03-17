#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXTERNAL_DIR="${ROOT_DIR}/tests/external"
LOCK_FILE="${EXTERNAL_DIR}/lock.json"
UPDATE=0

if [[ $# -gt 1 ]]; then
  echo "usage: $0 [--update]"
  exit 1
fi
if [[ $# -eq 1 ]]; then
  if [[ "$1" == "--update" ]]; then
    UPDATE=1
  else
    echo "error: unknown option '$1'"
    exit 1
  fi
fi

mkdir -p "${EXTERNAL_DIR}"

clone_or_update() {
  local name="$1"
  local url="$2"
  local recursive="$3"
  local path="${EXTERNAL_DIR}/${name}"

  if [[ ! -d "${path}/.git" ]]; then
    echo "[clone] ${name}"
    if [[ "${recursive}" == "yes" ]]; then
      git clone --recursive "${url}" "${path}"
    else
      git clone "${url}" "${path}"
    fi
    return
  fi

  if [[ "${UPDATE}" -eq 1 ]]; then
    echo "[update] ${name}"
    git -C "${path}" fetch --all --tags --prune
    git -C "${path}" pull --ff-only
    if [[ "${recursive}" == "yes" ]]; then
      git -C "${path}" submodule sync --recursive
      git -C "${path}" submodule update --init --recursive
    fi
  else
    echo "[keep] ${name} (use --update to pull latest)"
    if [[ "${recursive}" == "yes" ]]; then
      git -C "${path}" submodule update --init --recursive
    fi
  fi
}

clone_or_update "open-test-cases" "https://github.com/pku-minic/open-test-cases.git" yes
clone_or_update "compiler-dev-test-cases" "https://github.com/pku-minic/compiler-dev-test-cases.git" no
clone_or_update "sysy-testsuit-collection" "https://github.com/jokerwyt/sysy-testsuit-collection.git" no

open_commit="$(git -C "${EXTERNAL_DIR}/open-test-cases" rev-parse HEAD)"
open_sysy_commit=""
if [[ -d "${EXTERNAL_DIR}/open-test-cases/sysy" ]]; then
  open_sysy_commit="$(git -C "${EXTERNAL_DIR}/open-test-cases/sysy" rev-parse HEAD || true)"
fi

dev_commit="$(git -C "${EXTERNAL_DIR}/compiler-dev-test-cases" rev-parse HEAD)"
lvx_commit="$(git -C "${EXTERNAL_DIR}/sysy-testsuit-collection" rev-parse HEAD)"
now="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

cat >"${LOCK_FILE}" <<JSON
{
  "generated_at": "${now}",
  "repos": [
    {
      "name": "open-test-cases",
      "url": "https://github.com/pku-minic/open-test-cases.git",
      "commit": "${open_commit}",
      "sysy_submodule": {
        "url": "https://gitlab.eduxiji.net/nscscc/compiler2021.git",
        "commit": "${open_sysy_commit}"
      }
    },
    {
      "name": "compiler-dev-test-cases",
      "url": "https://github.com/pku-minic/compiler-dev-test-cases.git",
      "commit": "${dev_commit}"
    },
    {
      "name": "sysy-testsuit-collection",
      "url": "https://github.com/jokerwyt/sysy-testsuit-collection.git",
      "commit": "${lvx_commit}"
    }
  ]
}
JSON

echo "wrote ${LOCK_FILE}"
