#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INDEX_CSV="${ROOT_DIR}/tests/.out/suites/index.csv"
OUT_CSV="${ROOT_DIR}/tests/.out/suites/sanity.csv"

if [[ ! -f "${INDEX_CSV}" ]]; then
  echo "missing ${INDEX_CSV}; run scripts/suite-index.sh first"
  exit 1
fi

python3 - "${INDEX_CSV}" "${OUT_CSV}" <<'PY'
import csv
import sys
from pathlib import Path

index_csv = Path(sys.argv[1])
out_csv = Path(sys.argv[2])
out_csv.parent.mkdir(parents=True, exist_ok=True)

issues = []

with index_csv.open("r", encoding="utf-8", errors="ignore", newline="") as f:
    reader = csv.DictReader(f)
    for row in reader:
        suite = row["suite"]
        case_id = row["case_id"]
        kind = row["kind"]
        src = Path(row["src"])
        in_file = Path(row["in"])
        out_file = Path(row["out"])
        enabled = row["enabled"] == "1"
        if not enabled:
            continue

        if not src.exists():
            issues.append((suite, case_id, "missing_src", str(src)))
            continue
        if not out_file.exists():
            issues.append((suite, case_id, "missing_out", str(out_file)))
            continue

        in_exists = in_file.exists()
        in_size = in_file.stat().st_size if in_exists else 0
        out_size = out_file.stat().st_size

        # Heuristic: very large expected output with tiny input often indicates
        # mismatched in/out pairing in dataset packaging.
        if kind == "perf" and in_exists and out_size >= 200000 and in_size <= 2000:
            issues.append((suite, case_id, "size_skew", f"in={in_size},out={out_size}"))

with out_csv.open("w", encoding="utf-8", newline="") as f:
    w = csv.writer(f)
    w.writerow(["suite", "case_id", "issue", "detail"])
    w.writerows(issues)

print(f"wrote {out_csv} ({len(issues)} issues)")
PY
