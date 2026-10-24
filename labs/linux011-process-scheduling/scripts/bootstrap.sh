#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/.work/hit-os"
PIN="01147f5bb8e0d2d9bdafbc51c5aa6b99dbb0f035"
if [ -e "$UPSTREAM" ]; then
  echo "Refusing to overwrite existing checkout: $UPSTREAM" >&2; exit 2
fi
mkdir -p "$ROOT/.work"
git clone https://github.com/appleweiping/hit-os.git "$UPSTREAM"
git -C "$UPSTREAM" checkout --detach "$PIN"
ACTUAL="$(git -C "$UPSTREAM" rev-parse HEAD)"
[ "$ACTUAL" = "$PIN" ]
cp "$ROOT/process.c" "$UPSTREAM/experiments/03-process-trace/user/process.c"
echo "Pinned Linux 0.11 lab upstream: $ACTUAL"
echo "Next: export OSLAB=/absolute/path/to/oslab ; bash scripts/run_guest.sh"
