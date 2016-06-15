#include "syscall.h"
#include <sched.h>

int sched_rr_get_interval(pid_t pid, struct timespec* ts) {
    return syscall(SYS_sched_rr_get_interval, pid, ts);
}
