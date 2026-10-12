# shm-frame-bench

A minimal shared-memory producer–consumer pipeline for uncompressed video frames on Linux, with a reproducible latency benchmark.

It is a baseline for measuring how much latency and jitter come from moving frames through shared memory alone, before any media framework (such as MXL) adds its own overhead.

## How it works

```
 producer (fixed fps)            /dev/shm ring buffer             consumer
 ────────────────────            ───────────────────────          ────────────────────
 copy frame into slot k%N  ──▶  [hdr][slot0][slot1]...[slotN-1] ──▶ copy frame out
 stamp t_publish                 write_seq / read_seq counters      latency = now - t_publish
 publish (write_seq++)                                              read_seq++
 ring full? → drop frame
```

- **Single producer, single consumer** ring buffer with `N` slots (default 8).
- Counters use C11 atomics with release/acquire ordering, so the consumer never sees a half-written frame.
- Counters sit on separate cache lines to avoid false sharing between the two CPUs.
- The producer is paced with absolute-time `clock_nanosleep`, so frame timing doesn't drift.
- If the ring is full, the producer **drops** the frame and counts it, because a live pipeline can't wait. Dropped frames = missed real-time deadlines.
- The consumer busy-waits for the lowest latency, at the cost of one CPU core.

## Build and run

```bash
make
# ./run.sh <frame_bytes> <fps> <num_frames> [label] [consumer_cpu] [producer_cpu]
./run.sh 5529600 50 500 1080p_native          # 1080p V210, 50 fps, 10 s
./run.sh 5529600 50 500 1080p_pinned 1 0      # same, pinned to CPUs
./run.sh 22118400 50 300 4k_native            # 4K V210
```

Frame sizes for V210 (10-bit 4:2:2, 16 bytes per 6 pixels):

| Format | Bytes per frame |
|---|---|
| 1080p | 5,529,600 |
| 2160p (4K) | 22,118,400 |

Each run writes per-frame latencies to `results/<label>.csv` and prints mean, p50, p99, p99.9, max, jitter (std. dev.) and dropped frames.

### Repeated runs (for real experiments)

A single run is noise. `sweep.sh` runs one configuration several times and appends one row per run to `results/summary.csv`:

```bash
# ./sweep.sh <label> <reps> <frame_bytes> <fps> <num_frames> [consumer_cpu] [producer_cpu]
./sweep.sh 1080p_native 10 5529600 50 500
./sweep.sh 1080p_pinned 10 5529600 50 500 1 0
./sweep.sh 4k_native    10 22118400 50 300
```

### Adding load (contention experiments)

`stress` is a small load generator included so no extra install (or sudo) is needed:

```bash
./stress cpu 4 60 &    # 4 workers burning CPU for 60 s
./sweep.sh 1080p_cpu4 10 5529600 50 500
./stress mem 2 60 &    # 2 workers saturating memory bandwidth for 60 s
./sweep.sh 1080p_mem2 10 5529600 50 500
```

Make sure the stress duration is longer than the whole sweep.

### Running on Windows

This needs Linux (`shm_open`, `mmap`, `taskset`). On Windows, run it inside WSL2 (Ubuntu). WSL2 is a virtual machine, so expect more latency noise than on bare-metal Linux, and always state the environment next to your results.

## Results

Fill this in with your own machine's runs. Run each configuration several times and report the spread, not a single run.

| Config | p50 (µs) | p99 (µs) | p99.9 (µs) | Jitter (µs) | Dropped |
|---|---|---|---|---|---|
| 1080p native | | | | | |
| 1080p pinned | | | | | |
| 4K native | | | | | |

**Test machine:** CPU model, cores, RAM, kernel version (`uname -r`).

## What latency includes here

Measured from the moment the producer finishes writing a frame to the moment the consumer has copied it out. It includes the consumer's wake-up delay and the frame copy (`memcpy`). It does **not** include the producer's own copy.

## Limitations

- One producer and one consumer only.
- Busy-waiting consumer (no futex or eventfd notification).
- Same host only; no network or RDMA path.
