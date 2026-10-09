#!/usr/bin/env python3
"""Across-run uncertainty is distinct from within-run sampled lock-wait tails."""
import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path
p = argparse.ArgumentParser(); p.add_argument('directory'); a = p.parse_args()
out = Path(a.directory)
rows = [json.loads(x) for x in (out / 'raw.jsonl').read_text().splitlines()]
groups = defaultdict(list)
for r in rows: groups[(r['mode'], r['threads'], r['hold_us'], r['placement'])].append(r)
fields = ['throughput', 'wait_p50_ns', 'wait_p95_ns', 'wait_p99_ns', 'voluntary_cs', 'involuntary_cs', 'cpu_ns', 'wall_ns']
summary = []
for key, values in sorted(groups.items()):
    rec = dict(zip(['mode', 'threads', 'hold_us', 'placement'], key)); rec['repeats'] = len(values)
    for name in fields:
        data = [v[name] for v in values]
        rec[name + '_median'] = statistics.median(data)
        if len(data) > 1:
            quartiles = statistics.quantiles(data, n=4, method='inclusive')
            rec[name + '_q1'], rec[name + '_q3'] = quartiles[0], quartiles[2]
    summary.append(rec)
with (out / 'summary.csv').open('w') as f:
    w = csv.DictWriter(f, fieldnames=list(summary[0])); w.writeheader(); w.writerows(summary)
try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
except ImportError:
    raise SystemExit('CSV saved. Install matplotlib to generate PNG/SVG plots.')
holds = sorted({r['hold_us'] for r in summary})
fig, axes = plt.subplots(2, len(holds), figsize=(5 * len(holds), 7), squeeze=False)
colors = {'shared': '#D45A43', 'sharded': '#276FB0'}
for j, hold in enumerate(holds):
    for mode in ['shared', 'sharded']:
        for placement in ['single', 'spread']:
            data = sorted([r for r in summary if r['hold_us'] == hold and r['mode'] == mode and r['placement'] == placement], key=lambda r:r['threads'])
            for i, metric in enumerate(['throughput', 'wait_p99_ns']):
                scale = 1e6 if i == 0 else 1e3
                axes[i,j].plot([r['threads'] for r in data], [r[metric + '_median']/scale for r in data],
                               marker='o', color=colors[mode], linestyle='-' if placement == 'spread' else '--',
                               label=mode + '/' + placement)
                axes[i,j].fill_between([r['threads'] for r in data],
                                      [r[metric + '_q1']/scale for r in data], [r[metric + '_q3']/scale for r in data],
                                      color=colors[mode], alpha=.08)
    axes[0,j].set_title(f'Critical-section CPU work: {hold} us/op')
    for i in range(2):
        axes[i,j].set_xlabel('Worker threads'); axes[i,j].grid(alpha=.2); axes[i,j].set_xticks([1,2,4,8])
    axes[0,j].set_ylabel('Throughput (million operations/s)')
    axes[1,j].set_ylabel('Sampled acquisition p99 (us)')
    axes[1,j].set_yscale('log')
axes[0,0].legend(fontsize=8)
fig.suptitle('Linux mutex baseline vs sharding\nLines: across-run medians; bands: across-run IQR', fontsize=13)
fig.tight_layout(rect=(0,0,1,.92))
fig.savefig(out / 'comparison.png', dpi=180); fig.savefig(out / 'comparison.svg')
print('Saved summary.csv, comparison.png and comparison.svg')
