#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/.work/hit-os"
LAB="$ROOT/.work/lab"
if [ -z "${OSLAB:-}" ]; then
  echo 'Set OSLAB to local course oslab resources first.' >&2; exit 2
fi
[ -f "$UPSTREAM/scripts/run.sh" ] || { echo 'Run scripts/bootstrap.sh first.' >&2; exit 2; }
for file in "$OSLAB/hdc-0.11.img" "$OSLAB/bochs/BIOS-bochs-latest" "$OSLAB/bochs/vgabios.bin"; do
  [ -f "$file" ] || { echo "Missing: $file" >&2; exit 2; }
done
for bin in gcc-3.4 as86 ld86 bochs Xvfb import; do
  command -v "$bin" >/dev/null || { echo "Missing: $bin" >&2; exit 2; }
done
for bin in bochs Xvfb; do
  if pgrep -x "$bin" >/dev/null 2>&1; then
    echo "Other $bin process exists; refusing to run upstream harness." >&2; exit 2
  fi
done
if [ ! -f "$LAB/hdc-master.img" ]; then
  mkdir -p "$LAB"
  cp "$OSLAB/hdc-0.11.img" "$LAB/hdc-master.img"
fi
OUT="$ROOT/.work/guest-$(date +%Y%m%d-%H%M%S)"
[ ! -e "$OUT" ] || { echo "Refusing to overwrite $OUT" >&2; exit 2; }
mkdir -p "$OUT"
( cd "$UPSTREAM" && REPO="$UPSTREAM" LAB="$LAB" OSLAB="${OSLAB:-}" bash scripts/run.sh 03 160 ) | tee "$OUT/run.log"
[ -s "$UPSTREAM/results/03.png" ] || { echo 'Missing Bochs screenshot.' >&2; exit 1; }
[ -s "$UPSTREAM/results/03__var_process.log" ] || { echo 'Missing guest process.log.' >&2; exit 1; }
cp "$UPSTREAM/results/03.png" "$OUT/linux011-bochs.png"
cp "$UPSTREAM/results/03__var_process.log" "$OUT/process.log"
python3 "$UPSTREAM/experiments/03-process-trace/stat_log.py" "$OUT/process.log" "$OUT/timeline.png" | tee "$OUT/statistics.txt"
echo "Linux 0.11 guest evidence saved in $OUT"
