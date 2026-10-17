#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 SOURCE_DIRECTORY [OUTPUT_BINARY]" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CANDIDATE="$(cd "$1" && pwd)"
OUT="${2:-$ROOT/../../.build/touch-contract-test}"
mkdir -p "$(dirname "$OUT")"

for file in touch_calibration_core.h touch_calibration_core.cpp; do
  if [[ ! -f "$CANDIDATE/$file" ]]; then
    echo "Missing $CANDIDATE/$file" >&2
    exit 2
  fi
done

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$CANDIDATE" \
  "$ROOT/test_contract.cpp" \
  "$CANDIDATE/touch_calibration_core.cpp" \
  -o "$OUT"

"$OUT"
