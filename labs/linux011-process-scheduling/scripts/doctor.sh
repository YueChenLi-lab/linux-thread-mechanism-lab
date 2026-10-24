#!/usr/bin/env bash
set -euo pipefail
echo '=== Linux 0.11 lab preflight ==='
uname -a
for bin in git python3 gcc make as86 ld86 gcc-3.4 bochs Xvfb import; do
  if command -v "$bin" >/dev/null 2>&1; then
    printf '[OK] %-10s %s\n' "$bin" "$(command -v "$bin")"
  else
    printf '[MISSING] %s\n' "$bin"
  fi
done
if [ -n "${OSLAB:-}" ]; then
  for name in hdc-0.11.img bochs/BIOS-bochs-latest bochs/vgabios.bin; do
    if [ -f "$OSLAB/$name" ]; then echo "[OK] $OSLAB/$name";
    else echo "[MISSING] $OSLAB/$name"; fi
  done
else
  echo '[MISSING] OSLAB environment variable (set path to local course oslab package)'
fi
echo 'Install if needed: sudo apt update && sudo apt install build-essential gcc-multilib bin86 bochs bochs-x xvfb imagemagick git python3'
echo 'Historic gcc-3.4 and oslab disk/BIOS resources are required separately.'
