#!/usr/bin/env python3
"""Mechanism-only runs. Never mix these timing values into performance CSV."""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(); p.add_argument('--out', default='results/traces'); a = p.parse_args()
out = ROOT / a.out; out.mkdir(parents=True, exist_ok=True)
if not shutil.which('strace'): raise SystemExit('strace required (on Ubuntu: sudo apt install strace)')
subprocess.run(['make', '-C', str(ROOT)], check=True)
cases = [('private_fast', 'sharded', '4', '2000', '0'), ('shared_zero', 'shared', '4', '2000', '0'),
         ('shared_long', 'shared', '4', '2000', '20')]
report = []
for name, mode, n, ops, hold in cases:
    target = out / (name + '.strace')
    if target.exists(): raise SystemExit('use a new --out directory')
    cmd = ['strace', '-f', '-ttt', '-T', '-s', '1024', '-e', 'trace=clone,clone3,futex,write',
           '-o', str(target), str(ROOT / 'build/contention'), mode, n, ops, hold, 'spread']
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    (out / (name + '.stderr')).write_text(r.stderr)
    (out / (name + '.stdout')).write_text(r.stdout)
    if r.returncode:
        (out / (name + '.error.json')).write_text(json.dumps({'returncode':r.returncode,'error':r.stderr}))
        raise SystemExit('strace failed: inspect saved stderr; ptrace may be restricted')
    locks = set(re.search(r'MEASURE_BEGIN locks=(.*)', r.stderr).group(1).split(','))
    counts = {'wait_calls': 0, 'wake_calls': 0, 'eagain_visible': 0, 'target_futex_calls': 0}
    measured = False
    for line in target.read_text().splitlines():
        if 'MEASURE_BEGIN' in line: measured = True
        if 'MEASURE_END' in line: measured = False
        if not measured: continue
        match = re.search(r'futex\((0x[0-9a-f]+), ([A-Z_|]+)', line)
        if not match or match.group(1) not in locks: continue
        counts['target_futex_calls'] += 1
        counts['wait_calls'] += 'WAIT' in match.group(2)
        counts['wake_calls'] += 'WAKE' in match.group(2)
        counts['eagain_visible'] += 'EAGAIN' in line
    report.append({'case': name, 'locks': sorted(locks), **counts,
                   'note': 'Entry counts only. WAIT may return EAGAIN; it does not prove sleeping. Glibc mutex base-word mapping; manually inspect traces.'})
(out / 'trace_summary.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
