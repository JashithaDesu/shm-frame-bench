# shm-frame-bench

A minimal shared-memory producer–consumer pipeline for uncompressed video frames on Linux, plus a reproducible latency benchmark.

It serves as a baseline: how much latency and jitter come from moving frames through shared memory alone. The same benchmark also drives a writer/reader built on the [MXL SDK](https://github.com/dmf-mxl/mxl) (Media eXchange Layer, built from source, v1.2.0-dev), so the two can be compared directly.

![Power profile result](docs/power_mode.png)

## Key findings

On a Dell Latitude 7490 (Intel i5-8350U, 4 cores / 8 threads):

1. **MXL's data path added no measurable latency over raw shared memory; its waiting strategy did.** With a busy-polling reader, MXL and the minimal pipeline were indistinguishable at 1080p50 (p99 855 vs. 875 µs, p = 0.13). MXL's normal blocking reader, which sleeps until each commit, cost about 70–90 µs extra at p99 (943 µs, p < 0.001) — about 0.5% of the 20 ms frame budget — in exchange for not burning a CPU core.
2. **A sleeping reader was more robust to a noisy neighbour.** When a stray busy-looping process accidentally shared the reader's CPU, both busy-polling readers' p99 jumped to about 3.8 ms (roughly one scheduler time slice), while MXL's blocking reader stayed at 2.3 ms.
3. **The CPU power profile dominated everything else.** In a randomized, interleaved test, the `performance` profile cut 1080p median latency by **45%** (1,257 → 686 µs) and p99 by **40%** (1,591 → 957 µs) compared with `power-saver` (p < 0.001 for both). Every single performance run was faster than every power-saver run.
4. **In power-saver mode, 4K missed its real-time budget.** At 50 fps each frame has 20 ms. The 4K p99.9 reached up to 26 ms, and only the 8-slot ring buffer prevented dropped frames. In performance mode, 4K p99.9 stayed at 5–6 ms.
5. **CPU pinning helped only when the CPU was slow.** In power-saver mode, pinning reduced 1080p p99 by about 10% (p < 0.01). In performance mode the effect disappeared (p = 0.70).
6. **Core placement showed a power-mode-dependent effect.** In power-saver mode, placing producer and consumer on the *same* physical core cut 4K p99 by 21% (p < 0.001). In performance mode there was no significant difference. A CPU-frequency explanation was tested and ruled out (same clock speed in both placements); the mechanism remains open.

**Takeaways:** on this machine the exchange layer itself was not the bottleneck — the frame copy, the reader's waiting strategy, the CPU power policy and other processes on the same CPU were. Tuning advice such as CPU pinning depends on the platform's power settings, and benchmark results that don't report the power profile and background load are not reproducible.

## How it works

```
 producer (fixed fps)            /dev/shm ring buffer             consumer
 ────────────────────            ───────────────────────          ────────────────────
 copy frame into slot k%N  ──▶  [hdr][slot0][slot1]...[slotN-1] ──▶ copy frame out
 stamp t_publish                 write_seq / read_seq counters      latency = now - t_publish
 publish (write_seq++)                                              read_seq++
 ring full? → drop frame
```

- Single-producer, single-consumer ring buffer with 8 slots in POSIX shared memory (`shm_open` + `mmap`).
- C11 atomics with release/acquire ordering, so the consumer never reads a half-written frame.
- Counters on separate cache lines to avoid false sharing.
- Producer paced with absolute-time `clock_nanosleep`, so timing doesn't drift.
- If the ring is full, the producer drops the frame and counts it (a missed real-time deadline).
- Busy-waiting consumer: lowest latency, at the cost of one CPU core.

**What latency measures:** from the moment the producer publishes a frame to the moment the consumer has copied it out. It includes the consumer's wake-up delay and the frame copy, not the producer's own copy.

## Method

- **Frames:** V210 (10-bit 4:2:2) sizes — 1080p = 5,529,600 B, 4K = 22,118,400 B — plus a 64 KB small-frame case. 50 fps, 1,500 frames (30 s) per run.
- **Randomized, interleaved runs:** each round runs every configuration once, in shuffled order (`interleave.sh`), so slow drift in the machine (heat, background tasks, CPU speed) spreads evenly across configurations.
- **Repetition:** 10–15 runs per configuration. Results report the median across runs.
- **Statistics:** permutation test on per-run values (`compare.py`, 10,000 shuffles, fixed seed). With several comparisons per experiment, significance was judged against a Bonferroni-corrected threshold.
- **Controls:** real hardware (no VM), on AC power, no other applications open, warm-up run before each experiment, CPU frequency and temperature sampled during runs (`freqmon.py`).

## Results

All values are medians across runs, in µs. "Dropped" was 0 in every configuration.

### Experiment 1b — CPU pinning, 1080p, power-saver (15 rounds)

| Config | p50 | p99 | p99 range |
|---|---|---|---|
| Unpinned | 1,347 | 1,859 | 1,634–1,965 |
| Same physical core (CPUs 1, 5) | 1,269 | 1,650 | 1,537–1,901 |
| Different cores (CPUs 1, 2) | 1,275 | 1,690 | 1,582–1,896 |

Unpinned vs. same core p = 0.009; unpinned vs. different p = 0.004; same vs. different p = 0.44.

### Experiment 2 — frame size × placement, power-saver (15 rounds)

| Config | p50 | p99 | p99.9 |
|---|---|---|---|
| 64 KB, unpinned | 17 | 30 | 69 |
| 64 KB, same core | 15 | 30 | 56 |
| 64 KB, different cores | 17 | 28 | 136 |
| 4K, unpinned | 5,456 | 19,175 | 23,679 |
| 4K, same core | 5,138 | 15,278 | 18,147 |
| 4K, different cores | 5,182 | 19,437 | 26,223 |

64 KB: no significant differences (p ≥ 0.23). 4K same vs. different: p < 0.001. Measured clock during 4K runs: 877–916 MHz in both placements, 45–47 °C.

### Experiment 3 — pinning and placement, performance profile (15 rounds)

| Config | p50 | p99 | p99.9 |
|---|---|---|---|
| 1080p, unpinned | 694 | 994 | 1,596 |
| 1080p, same core | 690 | 1,020 | 1,677 |
| 1080p, different cores | 697 | 1,010 | 1,628 |
| 4K, unpinned | 2,756 | 3,875 | 4,958 |
| 4K, same core | 2,742 | 3,983 | 5,900 |
| 4K, different cores | 2,744 | 3,792 | 5,052 |

1080p unpinned vs. different: p = 0.70. 4K same vs. different: p = 0.084.

### Experiment 4 — power profile, 1080p, interleaved (10 rounds)

| Mode | p50 | p99 |
|---|---|---|
| power-saver | 1,257 | 1,591 |
| performance | 686 | 957 |

p50: p < 0.001; p99: p < 0.001.

A second pass of the same loop degraded 2–4× in both modes at once. The cause was found later: an orphaned busy-waiting `consumer` from an interrupted run had been left spinning on CPU 1 (see Experiment 6). The second pass was excluded from the effect-size estimate, but it still showed the same direction (p99 4,652 vs. 3,766 µs, p < 0.001). Raw data for both passes is in `results_exp4_powermode/`.

### Experiment 5 — raw shared memory vs. MXL, 1080p50, performance profile (15 rounds)

Three configurations, interleaved in random order, all pinned to the same CPUs (reader on CPU 1, writer on CPU 2), with background load removed (no browser, Kubernetes test cluster stopped):

| Config | Reader waits by | p50 | p99 | p99 range | p99.9 |
|---|---|---|---|---|---|
| `shm` — this repo's pipeline | busy-waiting | 669 | 875 | 822–1,001 | 1,532 |
| `mxl_spin` — MXL writer/reader | busy-polling (`GetGrainNonBlocking`) | 692 | 855 | 810–1,019 | 1,281 |
| `mxl_block` — MXL writer/reader | sleeping (`GetGrain`, futex wake-up) | 771 | 943 | 909–1,132 | 1,761 |

shm vs. mxl_spin: p = 0.13 (no detectable difference). shm vs. mxl_block: +69 µs, p < 0.001. mxl_spin vs. mxl_block: +89 µs, p < 0.001. Both significant comparisons pass the Bonferroni-corrected threshold (0.017). No frames were dropped or missed.

### Experiment 6 — unplanned noisy neighbour (same configurations as Experiment 5)

The first attempt at Experiment 5 ran while an orphaned busy-waiting `consumer` (from an interrupted earlier run) was spinning at ~90% CPU on CPU 1 — the same CPU as every reader. It was not a designed experiment, so treat it as indicative, but the pattern is clear:

| Config | p50 | p99 | p99 range | p99.9 |
|---|---|---|---|---|
| `shm` (busy-waiting) | 1,294 | 3,777 | 3,721–4,088 | 4,843 |
| `mxl_spin` (busy-polling) | 1,352 | 3,789 | 3,764–3,895 | 5,686 |
| `mxl_block` (sleeping) | 1,275 | 2,271 | 2,261–2,297 | 3,252 |

Both busy-waiting readers had to take turns with the stray process, and their p99 settled at ~3.8 ms — about one scheduler time slice. The sleeping reader was woken directly by MXL's futex on each commit and was less affected. The orphan was found with `ps`, and `consumer.c` now has a watchdog that exits 5 s after the producer stops. Data: `results_exp5_contaminated/`.

## Limitations

- **One machine.** A 15 W laptop CPU with a single NUMA node; results may differ on server hardware, ARM, or multi-socket systems.
- **Cross-session comparisons are weak.** The same configuration differed by up to ~9% between sessions, so only configurations measured together in one interleaved experiment are compared directly. Experiments 2 and 3 (power-saver vs. performance) were run in separate sessions; Experiment 4 confirms the power-profile effect within one session.
- **Experiment 1a (not reported above)** ran configurations in blocks instead of interleaved. A 27% shift in one block turned out to be background interference, which is why all later experiments were randomized. Its data is kept in `results_exp1a/` as a record.
- **Background load in Experiments 1–4.** A local Kubernetes (kind) test cluster (~15% CPU in total) and a web browser were running. Interleaving spreads this evenly across configurations, but absolute numbers include it. Both were removed for Experiment 5.
- **MXL comparison differences:** MXL's ring held 10 grains vs. 8 slots here; MXL paces frames by its own grain index rather than `clock_nanosleep`; and MXL never makes the writer drop a frame — a slow reader instead gets "too late" and skips ahead (counted as `dropped` by `mxl_reader`). Both ran at 50 fps with identical 5,529,600-byte V210 frames.
- **MXL was tested with its Flow API on one host only**, one writer and one reader, 1080p. Not tested: the Fabrics API, multiple readers, 4K, containers.
- **Single producer, single consumer** only; no one-to-many fan-out, no network or RDMA path, no containers yet.
- **Same-host only.** The baseline consumer only busy-waits; a sleeping (futex) reader was tested only through MXL's blocking API, not in the baseline itself.
- **The same-core 4K effect in power-saver mode is unexplained.**

## Build and run

```bash
make
# one run:  ./run.sh <frame_bytes> <fps> <num_frames> [label] [consumer_cpu] [producer_cpu]
./run.sh 5529600 50 1500 test 1 2

# randomized experiment:  ./interleave.sh <rounds> <frame_bytes> <fps> <frames> label:ccpu:pcpu ...
./interleave.sh 15 5529600 50 1500 unp:: same:1:5 diff:1:2
python3 summarize.py
python3 compare.py unp diff          # permutation test on p99 (or add p50_us, p999_us)
```

Check and record the power profile before every session:
```bash
powerprofilesctl get
```

### Files

| File | Purpose |
|---|---|
| `common.h`, `producer.c`, `consumer.c` | Shared-memory ring buffer and the two processes |
| `run.sh` | One run, prints a latency summary |
| `sweep.sh` | One configuration, repeated N times |
| `interleave.sh` | Several configurations, randomized order per round |
| `analyze.py` | Percentiles and jitter from one run's latency CSV |
| `summarize.py` | Median per configuration across runs |
| `compare.py` | Permutation test between two configurations |
| `freqmon.py` | Samples CPU clock and temperature during a run (no sudo) |
| `stress.c` | CPU / memory-bandwidth load generator (no sudo) |
| `mxl/mxl_writer.c`, `mxl/mxl_reader.c` | The same pipeline built on the MXL Flow API (reader: `block` or `spin`) |
| `mxl/flow-*.json` | NMOS flow definitions (V210, 1080p50 and 2160p50) |
| `mxl/build.sh` | Builds the MXL programs against an MXL source build in `~/mxl` |
| `interleave_mxl.sh` | Randomized comparison of `shm`, `mxl-spin` and `mxl-block` |

### Building the MXL part

MXL was built from source without root access: CMake and Ninja via `pip install --user`, vcpkg cloned (full history) into `~/vcpkg`, and MXL configured with `cmake --preset Linux-GCC-Release -DBUILD_TESTS=OFF -DBUILD_DOCS=OFF -DBUILD_UTILS=OFF` after removing the test-only dependencies (`catch2`, `pcapplusplus`) from `vcpkg.json`. GStreamer development packages were not available, so MXL's GStreamer tools were not built. Then:

```bash
./mxl/build.sh
./interleave_mxl.sh 15 1080 1500 shm:shm:1:2 mxl-spin:mxl_spin:1:2 mxl-block:mxl_block:1:2
```

## Test machine

Dell Latitude 7490 · Intel Core i5-8350U (4 cores, 8 threads, 1 NUMA node) · 14 GiB RAM · Ubuntu, Linux 7.0.0-38-generic · gcc 15.2 · bare metal (no VM) · AC power.
