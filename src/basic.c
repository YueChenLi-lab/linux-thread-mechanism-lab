#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static pthread_barrier_t ready;
static pthread_mutex_t output = PTHREAD_MUTEX_INITIALIZER;
static atomic_int heartbeat;
static _Thread_local int tls_value;
static int shared_value = 42; /* read-only until join */

static void checked(int code) {
    if (code) { fprintf(stderr, "pthread: %s\n", strerror(code)); exit(1); }
}
static void *worker(void *arg) {
    int id = *(int *)arg, local = id;
    tls_value = 100 + id;
    checked(pthread_mutex_lock(&output));
    printf("worker=%d pid=%ld tid=%ld shared_addr=%p stack_addr=%p tls_addr=%p tls=%d\n",
           id, (long)getpid(), syscall(SYS_gettid), (void *)&shared_value,
           (void *)&local, (void *)&tls_value, tls_value);
    checked(pthread_mutex_unlock(&output));
    int rc = pthread_barrier_wait(&ready);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) checked(rc);
    if (id == 0) {
        struct timespec t = {.tv_sec = 0, .tv_nsec = 300000000};
        nanosleep(&t, NULL);
        checked(pthread_mutex_lock(&output));
        printf("sleeping_worker_woke heartbeat=%d\n", atomic_load(&heartbeat));
        checked(pthread_mutex_unlock(&output));
    } else {
        for (int i = 0; i < 10; ++i) {
            atomic_fetch_add(&heartbeat, 1);
            struct timespec t = {.tv_sec = 0, .tv_nsec = 10000000};
            nanosleep(&t, NULL);
        }
    }
    return NULL;
}
int main(void) {
    pthread_t threads[3]; int ids[3] = {0, 1, 2};
    checked(pthread_barrier_init(&ready, NULL, 4));
    for (int i = 0; i < 3; ++i) checked(pthread_create(&threads[i], NULL, worker, &ids[i]));
    printf("main pid=%ld tid=%ld; expected: 4 live kernel tasks\n", (long)getpid(), syscall(SYS_gettid));
    /* Workers remain at ready: /proc is observed before any worker can exit. */
    /* /proc/self works even when procfs and the caller use different PID namespaces. */
    const char *path = "/proc/self/task";
    printf("inspect %s (including main)\n", path);
    fflush(stdout);
    /* Print task IDs without invoking an extra process. */
    extern void list_tasks(const char *);
    list_tasks(path);
    int rc = pthread_barrier_wait(&ready);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) checked(rc);
    for (int i = 0; i < 3; ++i) checked(pthread_join(threads[i], NULL));
    checked(pthread_barrier_destroy(&ready));
    printf("final heartbeat=%d expected=20\n", atomic_load(&heartbeat));
    return atomic_load(&heartbeat) == 20 ? 0 : 2;
}
#include <dirent.h>
void list_tasks(const char *path) {
    DIR *d = opendir(path); if (!d) { perror("opendir"); exit(1); }
    struct dirent *e; int count = 0;
    while ((e = readdir(d))) if (e->d_name[0] != '.') { printf("procfs_tid=%s\n", e->d_name); ++count; }
    closedir(d); printf("kernel_task_count=%d expected=4\n", count);
    if (count != 4) exit(2);
}
