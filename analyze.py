"""analyze.py - turn a latency CSV into the numbers a benchmark report needs.

Usage:
  python3 analyze.py results/run.csv [dropped_frames]            # human-readable
  python3 analyze.py results/run.csv [dropped_frames] --row      # one CSV row
"""
import csv, statistics, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
as_row = "--row" in sys.argv

lat_us = sorted(int(r["latency_ns"]) / 1000 for r in csv.DictReader(open(args[0])))
dropped = int(args[1]) if len(args) > 1 else 0

def pct(p):
    # nearest-rank percentile: the value below which p% of frames fall
    return lat_us[min(len(lat_us) - 1, max(0, int(round(p / 100 * len(lat_us))) - 1))]

stats = {
    "frames": len(lat_us),
    "dropped": dropped,
    "mean_us": statistics.mean(lat_us),
    "p50_us": pct(50),
    "p99_us": pct(99),
    "p999_us": pct(99.9),
    "max_us": lat_us[-1],
    "jitter_us": statistics.pstdev(lat_us),
}

if as_row:
    print(",".join(f"{v:.1f}" if isinstance(v, float) else str(v) for v in stats.values()))
else:
    print(f"frames      {stats['frames']}  (dropped {dropped})")
    for k in ["mean_us", "p50_us", "p99_us", "p999_us", "max_us", "jitter_us"]:
        print(f"{k:<11} {stats[k]:10.1f}")
