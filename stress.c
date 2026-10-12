/* stress.c - simple load generator, used instead of stress-ng when it
 * isn't installed (no sudo needed).
 *
 * Usage: ./stress <cpu|mem> <workers> <seconds>
 *   cpu: each worker spins on arithmetic   -> competes for CPU time
 *   mem: each worker copies a 256 MB buffer -> competes for memory bandwidth
 *        (256 MB is far bigger than the CPU cache, so every copy goes to RAM)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void cpu_work(int seconds) {
    time_t end = time(NULL) + seconds;
    volatile double x = 1.0;
    while (time(NULL) < end)
        for (int i = 0; i < 1000000; i++) x = x * 1.0000001 + 0.0000001;
}

static void mem_work(int seconds) {
    size_t sz = 256u << 20;
    char *a = malloc(sz), *b = malloc(sz);
    memset(a, 1, sz);
    memset(b, 2, sz);
    time_t end = time(NULL) + seconds;
    while (time(NULL) < end) {
        memcpy(b, a, sz);
        memcpy(a, b, sz);
    }
    free(a);
    free(b);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <cpu|mem> <workers> <seconds>\n", argv[0]); return 1; }
    int workers = atoi(argv[2]), seconds = atoi(argv[3]);
    int mem = strcmp(argv[1], "mem") == 0;
    for (int i = 0; i < workers; i++) {
        if (fork() == 0) {
            if (mem) mem_work(seconds); else cpu_work(seconds);
            _exit(0);
        }
    }
    for (int i = 0; i < workers; i++) wait(NULL);
    return 0;
}
