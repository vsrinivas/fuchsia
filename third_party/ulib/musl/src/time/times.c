#include "syscall.h"
#include <sys/times.h>

clock_t times(struct tms* tms) {
    return __syscall(SYS_times, tms);
}
