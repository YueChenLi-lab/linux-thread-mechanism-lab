#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT="$HERE/.work/host-$(date +%Y%m%d-%H%M%S)"
if [ -e "$OUTPUT" ]; then echo "Refusing to overwrite $OUTPUT" >&2; exit 2; fi
mkdir -p "$OUTPUT"
gcc -std=c90 -Wall -Wextra -O2 "$HERE/process.c" -o "$OUTPUT/process"
"$OUTPUT/process" | tee "$OUTPUT/result.txt"
grep -q '^ALL_CHILDREN_REAPED=5$' "$OUTPUT/result.txt"
[ "$(grep -c '^CREATED P' "$OUTPUT/result.txt")" -eq 5 ]
[ "$(grep -c '^REAPED pid=' "$OUTPUT/result.txt")" -eq 5 ]
echo "HOST VALIDATION OK: $OUTPUT"
