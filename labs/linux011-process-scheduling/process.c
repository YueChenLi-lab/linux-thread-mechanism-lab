/* Five-child CPU/IO workload for POSIX Linux and legacy Linux 0.11. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/times.h>
#include <sys/wait.h>
#define LAB_HZ 100
#define CHILD_COUNT 5
static void cpuio_bound(int last, int cpu_time, int io_time)
{
    struct tms start, now;
    long elapsed;
    int i;
    while (last > 0) {
        if (cpu_time > 0) {
            times(&start);
            do {
                times(&now);
                elapsed = (now.tms_utime - start.tms_utime) +
                          (now.tms_stime - start.tms_stime);
            } while (elapsed < (long)cpu_time * LAB_HZ);
            last -= cpu_time;
        }
        if (last <= 0) break;
        if (io_time > 0) {
            for (i = 0; i < io_time; ++i) sleep(1);
            last -= io_time;
        }
    }
}
int main(void)
{
    int i, pid, status, returned, ok = 0;
    int cpu[CHILD_COUNT] = {1, 0, 1, 1, 2};
    int io[CHILD_COUNT]  = {0, 1, 1, 3, 2};
    printf("PARENT pid=%d starts %d children\n", (int)getpid(), CHILD_COUNT);
    fflush(stdout);
    for (i = 0; i < CHILD_COUNT; ++i) {
        pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            printf("CHILD_START P%d pid=%d cpu=%d io=%d\n", i + 1,
                   (int)getpid(), cpu[i], io[i]);
            fflush(stdout);
            cpuio_bound(4, cpu[i], io[i]);
            printf("CHILD_DONE P%d pid=%d\n", i + 1, (int)getpid());
            fflush(stdout);
            return 0;
        }
        printf("CREATED P%d pid=%d\n", i + 1, pid);
        fflush(stdout);
    }
    for (i = 0; i < CHILD_COUNT; ++i) {
        returned = wait(&status);
        if (returned < 0) { perror("wait"); return 2; }
        printf("REAPED pid=%d status=%d\n", returned, status);
        fflush(stdout);
        ++ok;
    }
    printf("ALL_CHILDREN_REAPED=%d\n", ok);
    return ok == CHILD_COUNT ? 0 : 3;
}
