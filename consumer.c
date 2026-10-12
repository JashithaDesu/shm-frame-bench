/* consumer.c - reads frames from the ring buffer and records, for each frame,
 * the latency from "producer published it" to "consumer has copied it out".
 *
 * Usage: ./consumer <shm_name> <latency_csv>
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <shm_name> <latency_csv>\n", argv[0]); return 1; }
    const char *name = argv[1];

    int fd;
    while ((fd = shm_open(name, O_RDWR, 0600)) < 0) usleep(1000);  /* wait for producer */
    struct stat st;
    /* wait until producer has sized the segment */
    do { fstat(fd, &st); } while (st.st_size == 0 && usleep(1000) == 0);
    header_t *h = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (h == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);
    while (h->frame_size == 0) usleep(100);

    unsigned char *dst = malloc(h->frame_size);   /* where we "process" the frame */
    FILE *out = fopen(argv[2], "w");
    fprintf(out, "seq,latency_ns\n");

    atomic_store_explicit(&h->consumer_ready, 1, memory_order_release);

    uint64_t r = 0;
    uint64_t t_last = now_ns();   /* watchdog: time of last progress */
    unsigned spins = 0;
    for (;;) {
        uint64_t w = atomic_load_explicit(&h->write_seq, memory_order_acquire);
        if (r == w) {
            if (atomic_load_explicit(&h->producer_done, memory_order_acquire) &&
                r == atomic_load_explicit(&h->write_seq, memory_order_acquire))
                break;
            /* Watchdog: if the producer died (killed, Ctrl+C), never spin
             * forever. Checked every 64k spins so the clock read is cheap. */
            if (++spins == 65536) {
                spins = 0;
                if (now_ns() - t_last > 5ull * 1000000000ull) {
                    fprintf(stderr, "consumer: no frames for 5 s, producer gone? exiting\n");
                    break;
                }
            }
            continue;   /* busy-wait: lowest latency, but burns one CPU core */
        }
        t_last = now_ns();
        unsigned char *slot = slot_ptr(h, r);
        slot_hdr_t *sh = (slot_hdr_t *)slot;
        memcpy(dst, slot + sizeof(slot_hdr_t), h->frame_size);
        uint64_t lat = now_ns() - sh->t_publish;
        fprintf(out, "%llu,%llu\n", (unsigned long long)sh->seq, (unsigned long long)lat);
        atomic_store_explicit(&h->read_seq, ++r, memory_order_release);
    }

    fclose(out);
    fprintf(stderr, "consumer: received %llu frames\n", (unsigned long long)r);
    munmap(h, (size_t)st.st_size);
    shm_unlink(name);
    free(dst);
    return 0;
}
