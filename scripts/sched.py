#!/usr/bin/env python3
"""Optional scheduler evidence, in a separate run from strace and benchmarks."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(); p.add_argument('--out', default='results/scheduler'); a = p.parse_args()
out = ROOT / a.out; out.mkdir(parents=True, exist_ok=True)
if not shutil.which('perf'): raise SystemExit('perf is unavailable; install matching linux-tools on the experiment host.')
subprocess.run(['make', '-C', str(ROOT)], check=True)
for mode in ['shared', 'sharded']:
    data = out / (mode + '.data')
    if data.exists(): raise SystemExit('use a new --out directory')
    cmd = ['perf', 'sched', 'record', '-o', str(data), '--', str(ROOT / 'build/contention'),
           mode, '4', '20000', '20', 'spread']
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    (out / (mode + '.stderr')).write_text(r.stderr)
    (out / (mode + '.stdout')).write_text(r.stdout)
    if r.returncode: raise SystemExit('perf recording failed; inspect saved stderr and use a host with recording permission.')
    with (out / (mode + '.timehist.txt')).open('w') as f:
        subprocess.run(['perf', 'sched', 'timehist', '-i', str(data)], stdout=f, check=True, timeout=120)
print('Saved scheduler traces. Filter timehist by worker TIDs in .stdout; do not use traced throughput as benchmark data.')
