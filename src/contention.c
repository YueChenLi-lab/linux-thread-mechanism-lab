#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

typedef struct { pthread_mutex_t mutex; uint64_t count; } Slot;
/* Both alignment and stride separate slots, rather than padding only. */
static unsigned char *slots;
static size_t stride;
static int nthreads, sharded, hold_us, cpu_list[CPU_SETSIZE], ncpus;
static uint64_t total_ops;
static pthread_barrier_t ready;
static atomic_int go;
static Slot *slot_at(int i) { return (Slot *)(slots + (size_t)i * stride); }
static uint64_t now_ns(void) {
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
static void ck(int rc) { if (rc) { fprintf(stderr, "pthread: %s\n", strerror(rc)); exit(1); } }
static uint64_t number(const char *s, uint64_t max) {
    errno = 0; char *end; unsigned long long v = strtoull(s, &end, 10);
    if (errno || !*s || *s == '-' || *end || v > max) { fprintf(stderr, "invalid number: %s\n", s); exit(2); }
    return v;
}
typedef struct {
    int id, cpu; long tid;
    uint64_t ops, end, cpu_ns, *samples; size_t nsamples;
    long voluntary, involuntary;
} Worker;
static void *work(void *p) {
    Worker *w = p;
    w->tid = syscall(SYS_gettid);
    cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(w->cpu, &mask);
    ck(pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask));
    int rc = pthread_barrier_wait(&ready);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) ck(rc);
    while (!atomic_load_explicit(&go, memory_order_acquire)) { /* startup only */ }
    struct rusage before, after;
    if (getrusage(RUSAGE_THREAD, &before)) { perror("getrusage"); exit(1); }
    struct timespec cpu_before, cpu_after;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_before);
    Slot *s = slot_at(sharded ? w->id : 0);
    for (uint64_t i = 0; i < w->ops; ++i) {
        int sample = (i % 64 == 0);
        uint64_t t0 = sample ? now_ns() : 0;
        ck(pthread_mutex_lock(&s->mutex));
        if (sample) w->samples[w->nsamples++] = now_ns() - t0;
        ++s->count;
        if (hold_us) {
            uint64_t until = now_ns() + (uint64_t)hold_us * 1000;
            while (now_ns() < until) { /* CPU work inside critical section; no sleep syscall */ }
        }
        ck(pthread_mutex_unlock(&s->mutex));
    }
    w->end = now_ns();
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_after);
    if (getrusage(RUSAGE_THREAD, &after)) { perror("getrusage"); exit(1); }
    w->cpu_ns = ((uint64_t)cpu_after.tv_sec * 1000000000ULL + cpu_after.tv_nsec)
              - ((uint64_t)cpu_before.tv_sec * 1000000000ULL + cpu_before.tv_nsec);
    w->voluntary = after.ru_nvcsw - before.ru_nvcsw;
    w->involuntary = after.ru_nivcsw - before.ru_nivcsw;
    return NULL;
}
static int cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s shared|sharded THREADS TOTAL_OPS HOLD_US single|spread\n", argv[0]); return 2;
    }
    if (strcmp(argv[1], "shared") && strcmp(argv[1], "sharded")) return 2;
    if (strcmp(argv[5], "single") && strcmp(argv[5], "spread")) return 2;
    sharded = !strcmp(argv[1], "sharded");
    nthreads = (int)number(argv[2], 128); total_ops = number(argv[3], 100000000);
    hold_us = (int)number(argv[4], 10000);
    if (!nthreads || total_ops < (uint64_t)nthreads) return 2;
    cpu_set_t allowed; if (sched_getaffinity(0, sizeof(allowed), &allowed)) { perror("sched_getaffinity"); return 1; }
    for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &allowed)) cpu_list[ncpus++] = i;
    if (!ncpus) return 1;
    /* Main polls on another allowed CPU where possible; workers' placement is explicit. */
    if (ncpus > 1) {
        cpu_set_t mainmask; CPU_ZERO(&mainmask); CPU_SET(cpu_list[ncpus - 1], &mainmask);
        if (sched_setaffinity(0, sizeof(mainmask), &mainmask)) { perror("sched_setaffinity"); return 1; }
    }
    stride = ((sizeof(Slot) + 127) / 128) * 128;
    ck(posix_memalign((void **)&slots, 128, stride * (size_t)nthreads));
    memset(slots, 0, stride * (size_t)nthreads);
    for (int i = 0; i < nthreads; ++i) ck(pthread_mutex_init(&slot_at(i)->mutex, NULL));
    Worker *ws = calloc((size_t)nthreads, sizeof(*ws));
    pthread_t *ts = calloc((size_t)nthreads, sizeof(*ts));
    if (!ws || !ts) return 1;
    ck(pthread_barrier_init(&ready, NULL, (unsigned)nthreads + 1));
    for (int i = 0; i < nthreads; ++i) {
        ws[i].id = i;
        int worker_cpus = ncpus > 1 ? ncpus - 1 : 1;
        ws[i].cpu = cpu_list[!strcmp(argv[5], "single") ? 0 : i % worker_cpus];
        ws[i].ops = total_ops / (uint64_t)nthreads + ((uint64_t)i < total_ops % (uint64_t)nthreads);
        ws[i].samples = calloc((size_t)(ws[i].ops / 64 + 1), sizeof(uint64_t));
        if (!ws[i].samples) return 1;
        ck(pthread_create(&ts[i], NULL, work, &ws[i]));
    }
    int rc = pthread_barrier_wait(&ready);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) ck(rc);
    /* Addresses are glibc mutex bases; correlate futex words only after inspecting trace. */
    fprintf(stderr, "MEASURE_BEGIN locks=");
    for (int i = 0; i < (sharded ? nthreads : 1); ++i) fprintf(stderr, "%s%p", i ? "," : "", (void *)&slot_at(i)->mutex);
    fprintf(stderr, "\n"); fflush(stderr);
    uint64_t start = now_ns();
    atomic_store_explicit(&go, 1, memory_order_release);
    for (int i = 0; i < nthreads; ++i) ck(pthread_join(ts[i], NULL));
    fprintf(stderr, "MEASURE_END\n");
    uint64_t end = start, count = 0, cpu_ns = 0;
    long voluntary = 0, involuntary = 0; size_t ns = 0;
    for (int i = 0; i < nthreads; ++i) {
        if (ws[i].end > end) end = ws[i].end;
        count += slot_at(i)->count; cpu_ns += ws[i].cpu_ns;
        voluntary += ws[i].voluntary; involuntary += ws[i].involuntary; ns += ws[i].nsamples;
    }
    uint64_t *samples = malloc(ns * sizeof(uint64_t)); if (!samples) return 1;
    size_t off = 0;
    for (int i = 0; i < nthreads; ++i) {
        memcpy(samples + off, ws[i].samples, ws[i].nsamples * sizeof(uint64_t)); off += ws[i].nsamples;
    }
    qsort(samples, ns, sizeof(uint64_t), cmp);
    printf("{\"mode\":\"%s\",\"threads\":%d,\"ops\":%" PRIu64 ",\"hold_us\":%d,\"placement\":\"%s\","
           "\"count\":%" PRIu64 ",\"wall_ns\":%" PRIu64 ",\"cpu_ns\":%" PRIu64 ","
           "\"throughput\":%.3f,\"wait_p50_ns\":%" PRIu64 ",\"wait_p95_ns\":%" PRIu64 ",\"wait_p99_ns\":%" PRIu64 ","
           "\"samples\":%zu,\"voluntary_cs\":%ld,\"involuntary_cs\":%ld,\"workers\":[",
           argv[1], nthreads, total_ops, hold_us, argv[5], count, end - start, cpu_ns,
           (double)total_ops * 1e9 / (double)(end - start), samples[(ns - 1) / 2],
           samples[(ns - 1) * 95 / 100], samples[(ns - 1) * 99 / 100], ns, voluntary, involuntary);
    for (int i = 0; i < nthreads; ++i)
        printf("%s{\"tid\":%ld,\"cpu\":%d,\"ops\":%" PRIu64 "}", i ? "," : "", ws[i].tid, ws[i].cpu, ws[i].ops);
    printf("]}\n");
    for (int i = 0; i < nthreads; ++i) { ck(pthread_mutex_destroy(&slot_at(i)->mutex)); free(ws[i].samples); }
    ck(pthread_barrier_destroy(&ready)); free(samples); free(slots); free(ws); free(ts);
    return count == total_ops ? 0 : 3;
}
