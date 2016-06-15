#include "syscall.h"
#include <errno.h>
#include <sched.h>

int sched_getparam(pid_t pid, struct sched_param* param) {
    return __syscall_ret(-ENOSYS);
}
