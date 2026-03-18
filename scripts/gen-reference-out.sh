#!/usr/bin/env bash
set -euo pipefail

echo "error: scripts/gen-reference-out.sh is deprecated."
echo "reason: baseline has migrated to official compiler2025 ZIP suites only."
echo "action: run scripts/suite-sync.sh [--src-root <path>] and scripts/suite-index.sh instead."
exit 2
