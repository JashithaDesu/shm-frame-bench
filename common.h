/* common.h - shared-memory layout used by producer and consumer.
 *
 * Memory layout in /dev/shm/<name>:
 *   [ header_t ][ slot 0 ][ slot 1 ] ... [ slot N-1 ]
 * Each slot:
 *   [ slot_hdr_t (seq, timestamp) ][ frame payload (frame_size bytes) ]
 *
 * It is a single-producer / single-consumer ring buffer.
 * write_seq = how many frames the producer has published.
 * read_seq  = how many frames the consumer has finished reading.
 * Frame number k lives in slot (k % slots).
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#define CACHE_LINE 64

typedef struct {
    /* Each counter on its own cache line to avoid "false sharing":
     * otherwise producer and consumer CPUs fight over the same line. */
    _Alignas(CACHE_LINE) _Atomic uint64_t write_seq;
    _Alignas(CACHE_LINE) _Atomic uint64_t read_seq;
    _Alignas(CACHE_LINE) _Atomic int consumer_ready;
    _Atomic int producer_done;
    uint64_t frame_size;
    uint32_t slots;
} header_t;

typedef struct {
    uint64_t seq;        /* frame number */
    uint64_t t_publish;  /* CLOCK_MONOTONIC ns when producer published it */
} slot_hdr_t;

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline size_t slot_stride(uint64_t frame_size) {
    size_t s = sizeof(slot_hdr_t) + frame_size;
    return (s + CACHE_LINE - 1) & ~(size_t)(CACHE_LINE - 1);  /* round up */
}

static inline size_t shm_total_size(uint64_t frame_size, uint32_t slots) {
    return sizeof(header_t) + (size_t)slots * slot_stride(frame_size);
}

static inline unsigned char *slot_ptr(header_t *h, uint64_t seq) {
    unsigned char *base = (unsigned char *)h + sizeof(header_t);
    return base + (seq % h->slots) * slot_stride(h->frame_size);
}

#endif
