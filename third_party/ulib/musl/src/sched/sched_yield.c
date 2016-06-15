#include "syscall.h"
#include <sched.h>

int sched_yield() {
    return syscall(SYS_sched_yield);
}
