/* producer.c - writes fake video frames into a shared-memory ring buffer
 * at a fixed frame rate, like a camera or decoder would.
 *
 * Usage: ./producer <shm_name> <frame_bytes> <fps> <num_frames> [slots]
 * Example (1080p V210, 50 fps, 10 s): ./producer /fb 5529600 50 500 8
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "common.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <shm_name> <frame_bytes> <fps> <num_frames> [slots]\n", argv[0]);
        return 1;
    }
    const char *name = argv[1];
    uint64_t frame_size = strtoull(argv[2], NULL, 10);
    double fps = atof(argv[3]);
    uint64_t n_frames = strtoull(argv[4], NULL, 10);
    uint32_t slots = argc > 5 ? (uint32_t)atoi(argv[5]) : 8;

    size_t total = shm_total_size(frame_size, slots);
    shm_unlink(name);                                   /* clean leftovers */
    int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, (off_t)total) != 0) { perror("ftruncate"); return 1; }
    header_t *h = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (h == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    memset(h, 0, sizeof(*h));
    h->frame_size = frame_size;
    h->slots = slots;

    /* A "source frame" we copy from, so each publish costs a real memcpy,
     * the way a media function writing a decoded frame would. */
    unsigned char *src = malloc(frame_size);
    memset(src, 0xAB, frame_size);

    fprintf(stderr, "producer: waiting for consumer...\n");
    while (!atomic_load_explicit(&h->consumer_ready, memory_order_acquire))
        usleep(1000);

    uint64_t period = (uint64_t)(1e9 / fps);
    uint64_t dropped = 0;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    for (uint64_t k = 0, published = 0; k < n_frames; k++) {
        uint64_t r = atomic_load_explicit(&h->read_seq, memory_order_acquire);
        if (published - r >= slots) {
            /* Ring is full: consumer is too slow. A live system can't wait,
             * so we drop this frame (a "missed real-time deadline"). */
            dropped++;
        } else {
            unsigned char *slot = slot_ptr(h, published);
            memcpy(slot + sizeof(slot_hdr_t), src, frame_size);
            slot_hdr_t *sh = (slot_hdr_t *)slot;
            sh->seq = published;
            sh->t_publish = now_ns();
            /* release: the frame data above is visible before the counter. */
            atomic_store_explicit(&h->write_seq, ++published, memory_order_release);
        }
        /* Sleep until the next frame tick (absolute time = no drift). */
        next.tv_nsec += (long)period;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }

    atomic_store_explicit(&h->producer_done, 1, memory_order_release);
    fprintf(stderr, "producer: sent %llu, dropped %llu\n",
            (unsigned long long)(n_frames - dropped), (unsigned long long)dropped);
    printf("dropped=%llu\n", (unsigned long long)dropped);
    free(src);
    munmap(h, total);
    return 0;
}
