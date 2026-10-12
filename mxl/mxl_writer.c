/* mxl_writer.c - the MXL equivalent of producer.c.
 *
 * Creates (or opens) an MXL video flow and writes one grain (= one frame)
 * per frame period. Like producer.c, every frame costs a real memcpy of a
 * full frame, and the publish time is stamped right before the commit, so
 * the reader can measure "commit -> frame copied out" latency.
 *
 * Usage: ./mxl_writer <domain_dir> <flow.json> <num_frames>
 *   It writes num_frames + 20 grains: the reader discards the first 10
 *   as warm-up, and the extra 10 give it margin to finish.
 *
 * Differences from producer.c, on purpose (MXL decides these, not us):
 *   - Frame timing comes from MXL's grain index (wall-clock based), not
 *     from our own clock_nanosleep loop.
 *   - The writer never waits for, or drops for, a slow reader: MXL's ring
 *     simply moves on, and a reader that falls too far behind gets
 *     MXL_ERR_OUT_OF_RANGE_TOO_LATE (counted as "missed" by mxl_reader).
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

/* Same clock as consumer.c / mxl_reader.c, so latencies are comparable. */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <domain_dir> <flow.json> <num_frames>\n", argv[0]);
        return 1;
    }
    const char *domain = argv[1];
    uint64_t n_frames = strtoull(argv[3], NULL, 10) + 20;

    char *flow_def = read_file(argv[2]);
    if (!flow_def) return 1;

    mxlInstance inst = mxlCreateInstance(domain, NULL);
    if (!inst) { fprintf(stderr, "writer: mxlCreateInstance failed (does %s exist?)\n", domain); return 1; }

    mxlFlowWriter writer;
    mxlFlowConfigInfo cfg;
    bool created = false;
    mxlStatus st = mxlCreateFlowWriter(inst, flow_def, NULL, &writer, &cfg, &created);
    if (st != MXL_STATUS_OK) { fprintf(stderr, "writer: mxlCreateFlowWriter failed, status %d\n", (int)st); return 1; }

    mxlRational rate = cfg.common.grainRate;
    fprintf(stderr, "writer: flow %s, %lld/%lld fps, %u grains in ring\n",
            created ? "created" : "opened", (long long)rate.numerator, (long long)rate.denominator,
            cfg.discrete.grainCount);

    /* Give the reader time to attach before the first frame. */
    usleep(1000 * 1000);

    unsigned char *src = NULL;   /* the "source frame" copied into every grain */
    uint64_t idx = mxlGetCurrentIndex(&rate) + 1;

    for (uint64_t k = 0; k < n_frames; k++, idx++) {
        /* Wait until this grain's time slot, like a camera producing frames. */
        mxlSleepForNs(mxlGetNsUntilIndex(idx, &rate));

        mxlGrainInfo gi;
        memset(&gi, 0, sizeof gi);
        uint8_t *payload = NULL;
        st = mxlFlowWriterOpenGrain(writer, idx, &gi, &payload);
        if (st != MXL_STATUS_OK) { fprintf(stderr, "writer: OpenGrain(%llu) failed, status %d\n", (unsigned long long)idx, (int)st); break; }

        if (!src) {
            src = malloc(gi.grainSize);
            memset(src, 0xAB, gi.grainSize);
            fprintf(stderr, "writer: grain size %u bytes, %u slices\n", gi.grainSize, gi.totalSlices);
        }

        memcpy(payload, src, gi.grainSize);
        /* Stamp the publish time into the first 8 bytes of the frame. */
        uint64_t t = now_ns();
        memcpy(payload, &t, sizeof t);

        gi.flags = 0;
        gi.validSlices = gi.totalSlices;     /* the whole frame is ready */
        st = mxlFlowWriterCommitGrain(writer, &gi);
        if (st != MXL_STATUS_OK) { fprintf(stderr, "writer: CommitGrain failed, status %d\n", (int)st); break; }
    }

    fprintf(stderr, "writer: done\n");
    free(src);
    free(flow_def);
    mxlReleaseFlowWriter(inst, writer);
    mxlDestroyInstance(inst);
    return 0;
}
