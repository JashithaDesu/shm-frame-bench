"""summarize.py - one line per configuration from results/summary.csv.
Shows the MEDIAN across repetitions (robust to one bad run) and the
min-max range of p99, so you can see how stable each config is.
Usage: python3 summarize.py
"""
import csv, statistics
from collections import defaultdict

runs = defaultdict(list)
for r in csv.DictReader(open("results/summary.csv")):
    runs[r["label"]].append(r)

print(f"{'config':<16}{'reps':>5}{'p50 us':>10}{'p99 us':>10}{'p99 range':>20}{'p99.9 us':>11}{'dropped':>9}")
for label, rs in runs.items():
    med = lambda k: statistics.median(float(x[k]) for x in rs)
    p99s = [float(x["p99_us"]) for x in rs]
    dropped = sum(int(x["dropped"]) for x in rs)
    rng = f"{min(p99s):.0f}-{max(p99s):.0f}"
    print(f"{label:<16}{len(rs):>5}{med('p50_us'):>10.0f}{med('p99_us'):>10.0f}{rng:>20}{med('p999_us'):>11.0f}{dropped:>9}")
