#include "syscall.h"
#include <sys/resource.h>
#include <unistd.h>

int nice(int inc) {
#ifdef SYS_nice
    return syscall(SYS_nice, inc);
#else
    return setpriority(PRIO_PROCESS, 0, getpriority(PRIO_PROCESS, 0) + inc);
#endif
}
