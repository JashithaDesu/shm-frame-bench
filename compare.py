"""compare.py - is the difference between two configurations real, or noise?

Uses a permutation test on the per-run values (no extra libraries needed):
if the two configs were really the same, shuffling the run labels randomly
would often produce a difference as big as the one we measured.
The p-value is how often that happens.

Usage: python3 compare.py <label_A> <label_B> [metric]
  metric defaults to p99_us (others: p50_us, p999_us, mean_us, jitter_us)
"""
import csv, random, statistics, sys

a_label, b_label = sys.argv[1], sys.argv[2]
metric = sys.argv[3] if len(sys.argv) > 3 else "p99_us"
rows = list(csv.DictReader(open("results/summary.csv")))
a = [float(r[metric]) for r in rows if r["label"] == a_label]
b = [float(r[metric]) for r in rows if r["label"] == b_label]

observed = statistics.median(a) - statistics.median(b)
pooled = a + b
random.seed(42)          # fixed seed -> same answer every time you run it
shuffles, extreme = 10000, 0
for _ in range(shuffles):
    random.shuffle(pooled)
    d = statistics.median(pooled[:len(a)]) - statistics.median(pooled[len(a):])
    if abs(d) >= abs(observed):
        extreme += 1
p = extreme / shuffles

print(f"{metric}: {a_label} median {statistics.median(a):.0f} (n={len(a)})  vs  "
      f"{b_label} median {statistics.median(b):.0f} (n={len(b)})")
print(f"difference {observed:+.0f} us   p = {p:.4f}   ->  "
      + ("likely a REAL difference (p < 0.05)" if p < 0.05 else "could be NOISE (p >= 0.05)"))
