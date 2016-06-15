#include "syscall.h"
#include <threads.h>

void thrd_yield() {
    __syscall(SYS_sched_yield);
}
