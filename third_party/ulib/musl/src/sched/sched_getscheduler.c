#include "syscall.h"
#include <errno.h>
#include <sched.h>

int sched_getscheduler(pid_t pid) {
    return __syscall_ret(-ENOSYS);
}
