/* mxl_reader.c - the MXL equivalent of consumer.c.
 *
 * Reads grains from an MXL video flow, copies each frame out (like a media
 * function would), and records latency = now - publish time stamped by
 * mxl_writer. Output CSV has the same format as consumer.c, so analyze.py,
 * summarize.py and compare.py work unchanged.
 *
 * Usage: ./mxl_reader <domain_dir> <flow_id> <num_frames> <latency_csv> [block|spin]
 *   block (default): mxlFlowReaderGetGrain - sleeps until the writer commits
 *                    (MXL's normal way of waiting).
 *   spin:            mxlFlowReaderGetGrainNonBlocking in a busy loop - same
 *                    waiting strategy as consumer.c, for a like-for-like test.
 *
 * Prints "dropped=N" on stdout (grains missed because the reader fell more
 * than one ring behind), so run scripts can treat it like producer.c's output.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

#define WARMUP_FRAMES 10

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <domain_dir> <flow_id> <num_frames> <latency_csv> [block|spin]\n", argv[0]);
        return 1;
    }
    const char *domain = argv[1];
    const char *flow_id = argv[2];
    uint64_t n_frames = strtoull(argv[3], NULL, 10);
    int spin = (argc > 5 && strcmp(argv[5], "spin") == 0);

    mxlInstance inst = mxlCreateInstance(domain, NULL);
    if (!inst) { fprintf(stderr, "reader: mxlCreateInstance failed (does %s exist?)\n", domain); return 1; }

    /* Wait (up to 10 s) for the writer to create the flow. */
    mxlFlowReader reader;
    mxlStatus st = MXL_ERR_UNKNOWN;
    for (int i = 0; i < 10000; i++) {
        st = mxlCreateFlowReader(inst, flow_id, NULL, &reader);
        if (st == MXL_STATUS_OK) break;
        usleep(1000);
    }
    if (st != MXL_STATUS_OK) { fprintf(stderr, "reader: flow %s not found, status %d\n", flow_id, (int)st); return 1; }

    mxlFlowConfigInfo cfg;
    mxlFlowReaderGetConfigInfo(reader, &cfg);
    mxlRational rate = cfg.common.grainRate;

    FILE *out = fopen(argv[4], "w");
    if (!out) { perror(argv[4]); return 1; }
    fprintf(out, "seq,latency_ns\n");

    unsigned char *dst = NULL;
    uint64_t idx = mxlGetCurrentIndex(&rate);
    uint64_t received = 0, warm = 0, missed = 0, stale = 0;
    uint64_t t_start = now_ns(), t_last = t_start;

    while (received < n_frames) {
        mxlGrainInfo gi;
        uint8_t *payload = NULL;
        if (spin)
            st = mxlFlowReaderGetGrainNonBlocking(reader, idx, &gi, &payload);
        else
            st = mxlFlowReaderGetGrain(reader, idx, 200ull * 1000 * 1000, &gi, &payload);

        if (st == MXL_STATUS_OK) {
            /* MXL hands back whatever sits in that ring slot; a slot left over
             * from an earlier run carries a different index. Skip those. */
            if (gi.index != idx || (gi.flags & MXL_GRAIN_FLAG_INVALID)) { stale++; idx++; continue; }

            if (!dst) dst = malloc(gi.grainSize);
            memcpy(dst, payload, gi.grainSize);          /* "process" the frame */
            uint64_t t_pub;
            memcpy(&t_pub, dst, sizeof t_pub);           /* stamp from our copy */
            uint64_t t_now = now_ns();

            if (warm < WARMUP_FRAMES) {
                warm++;                                  /* discard start-up frames */
            } else {
                fprintf(out, "%llu,%llu\n", (unsigned long long)received,
                        (unsigned long long)(t_now - t_pub));
                received++;
            }
            t_last = t_now;
            idx++;
        } else if (st == MXL_ERR_OUT_OF_RANGE_TOO_LATE) {
            /* We fell more than one ring behind: jump to the newest grain. */
            mxlFlowRuntimeInfo rt;
            mxlFlowReaderGetRuntimeInfo(reader, &rt);
            if (received > 0 && rt.headIndex > idx) missed += rt.headIndex - idx;
            idx = rt.headIndex;
        } else if (st == MXL_ERR_OUT_OF_RANGE_TOO_EARLY || st == MXL_ERR_TIMEOUT) {
            uint64_t t_now = now_ns();
            if (warm + received == 0) {
                /* Writer not producing yet: keep pointing at "now". */
                if (!spin) idx = mxlGetCurrentIndex(&rate);
                if (t_now - t_start > 15ull * 1000000000ull) { fprintf(stderr, "reader: no frames after 15 s\n"); break; }
            } else if (t_now - t_last > 2ull * 1000000000ull) {
                fprintf(stderr, "reader: writer stopped early\n");
                break;
            }
        } else {
            fprintf(stderr, "reader: GetGrain(%llu) failed, status %d\n", (unsigned long long)idx, (int)st);
            break;
        }
    }

    fclose(out);
    fprintf(stderr, "reader (%s): received %llu, missed %llu, skipped stale %llu, ring %u grains\n",
            spin ? "spin" : "block", (unsigned long long)received, (unsigned long long)missed,
            (unsigned long long)stale, cfg.discrete.grainCount);
    printf("dropped=%llu\n", (unsigned long long)missed);

    free(dst);
    mxlReleaseFlowReader(inst, reader);
    mxlDestroyInstance(inst);
    return 0;
}
