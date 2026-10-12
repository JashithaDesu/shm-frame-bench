"""freqmon.py - sample CPU clock speed and temperature while a run happens.
Needs no sudo: reads /proc/cpuinfo and /sys/class/thermal.

Usage: python3 freqmon.py <seconds> <cpu,cpu,...>
Example (in the background during a 30 s run):
  python3 freqmon.py 28 1,2,5 > results/freq_same.txt &
"""
import glob, statistics, sys, time

seconds = float(sys.argv[1])
cpus = [int(c) for c in sys.argv[2].split(",")]

def mhz():
    out, cur = {}, None
    for line in open("/proc/cpuinfo"):
        if line.startswith("processor"):
            cur = int(line.split(":")[1])
        elif line.startswith("cpu MHz") and cur is not None:
            out[cur] = float(line.split(":")[1])
    return out

def pkg_temp():
    # prefer the CPU package sensor; fall back to the hottest zone
    temps = []
    for zone in glob.glob("/sys/class/thermal/thermal_zone*"):
        try:
            kind = open(zone + "/type").read().strip()
            t = int(open(zone + "/temp").read()) / 1000
        except OSError:
            continue
        if kind == "x86_pkg_temp":
            return t
        temps.append(t)
    return max(temps) if temps else float("nan")

samples = {c: [] for c in cpus}
temps = []
end = time.time() + seconds
while time.time() < end:
    m = mhz()
    for c in cpus:
        if c in m:
            samples[c].append(m[c])
    temps.append(pkg_temp())
    time.sleep(0.5)

for c in cpus:
    s = samples[c]
    if s:
        print(f"cpu{c}: mean {statistics.mean(s):.0f} MHz   min {min(s):.0f}   max {max(s):.0f}")
print(f"temp: mean {statistics.mean(temps):.1f} C   max {max(temps):.1f} C")
