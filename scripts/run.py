#!/usr/bin/env python3
"""Randomized, untraced performance runs; retains every repeat and environment."""
import argparse
import csv
import hashlib
import itertools
import json
import os
import platform
import random
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--quick', action='store_true')
p.add_argument('--out', default='results/full')
p.add_argument('--repeats', type=int)
a = p.parse_args()
repeats = a.repeats if a.repeats is not None else (3 if a.quick else 10)
if repeats < 2: p.error('at least 2 repeats required')
out = ROOT / a.out
out.mkdir(parents=True, exist_ok=True)
if (out / 'raw.jsonl').exists(): p.error('use a new --out directory to preserve prior results')
subprocess.run(['make', '-C', str(ROOT)], check=True)
def command(cmd):
    try: return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT).strip()
    except (OSError, subprocess.CalledProcessError) as e: return str(e)
def read_file(path):
    try: return Path(path).read_text().strip()
    except OSError: return None
cpu_ids = sorted(os.sched_getaffinity(0))
threads = [1, 2, 4, 8]
holds = [0, 10] if a.quick else [0, 5, 20]
ops = 20000 if a.quick else 200000
configs = list(itertools.product(['shared', 'sharded'], threads, holds, ['single', 'spread']))
env = {'date_utc': command(['date', '-u', '+%FT%TZ']), 'kernel': platform.uname()._asdict(),
       'nptl': command(['getconf', 'GNU_LIBPTHREAD_VERSION']), 'gcc': command(['gcc', '--version']),
       'affinity': cpu_ids, 'lscpu': command(['lscpu']), 'cgroup': read_file('/proc/self/cgroup'),
       'cpu_max_root': read_file('/sys/fs/cgroup/cpu.max'), 'cpu_stat_root': read_file('/sys/fs/cgroup/cpu.stat'),
       'config': {'ops': ops, 'holds': holds, 'threads': threads, 'repeats': repeats, 'seed': 20261009},
       'source_sha256': {str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest()
                         for f in sorted((ROOT / 'src').glob('*.c'))}}
(out / 'environment.json').write_text(json.dumps(env, ensure_ascii=False, indent=2))
def run(config):
    mode, n, hold, placement = config
    result = subprocess.run([ROOT / 'build/contention', mode, str(n), str(ops), str(hold), placement],
                            capture_output=True, text=True, check=True, timeout=180)
    row = json.loads(result.stdout)
    if row['count'] != ops: raise RuntimeError('count mismatch')
    return row
print(f'{len(configs)} configurations, {repeats} measured repeats; warmup first', flush=True)
for config in configs: run(config)
jobs = [(config, rep) for rep in range(repeats) for config in configs]
random.Random(20261009).shuffle(jobs)
with (out / 'raw.jsonl').open('w') as f:
    for idx, (config, rep) in enumerate(jobs):
        row = run(config); row['repeat'] = rep
        f.write(json.dumps(row) + '\n'); f.flush()
        if (idx + 1) % 16 == 0: print(f'{idx + 1}/{len(jobs)} measured runs complete', flush=True)
(out / 'cpu_stat_after.txt').write_text(read_file('/sys/fs/cgroup/cpu.stat') or 'unavailable')
print('Saved:', out, flush=True)
