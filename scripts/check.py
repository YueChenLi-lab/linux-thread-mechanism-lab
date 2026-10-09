#!/usr/bin/env python3
"""Meaningful checks: identities, live task count, progress and exact counts."""
import json
import re
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
basic = subprocess.check_output([ROOT / 'build/basic'], text=True, timeout=10)
rows = re.findall(r'worker=(\d+) pid=(\d+) tid=(\d+) shared_addr=(\S+) stack_addr=(\S+) tls_addr=(\S+) tls=(\d+)', basic)
assert len(rows) == 3, basic
assert len({r[1] for r in rows}) == 1
assert len({r[2] for r in rows}) == 3
assert len({r[3] for r in rows}) == 1
assert len({r[4] for r in rows}) == 3
assert len({r[5] for r in rows}) == 3
assert all(int(r[6]) == 100 + int(r[0]) for r in rows)
assert 'kernel_task_count=4 expected=4' in basic
assert 'sleeping_worker_woke heartbeat=20' in basic
for mode in ['shared', 'sharded']:
    for placement in ['single', 'spread']:
        for threads in [1, 2, 4]:
            result = subprocess.run([ROOT / 'build/contention', mode, str(threads), '10003', '1', placement],
                                    text=True, capture_output=True, check=True, timeout=15)
            record = json.loads(result.stdout)
            assert record['count'] == 10003
            assert sum(w['ops'] for w in record['workers']) == 10003
            assert len({w['tid'] for w in record['workers']}) == threads
            assert record['wait_p50_ns'] <= record['wait_p95_ns'] <= record['wait_p99_ns']
            assert record['wall_ns'] > 0 and record['samples'] > 0
            if placement == 'single': assert len({w['cpu'] for w in record['workers']}) == 1
print('PASS: thread identities, /proc count, independent progress, exact counters, placement and timing')
