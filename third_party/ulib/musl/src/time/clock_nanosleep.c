#include "libc.h"
#include "syscall.h"
#include <time.h>

int clock_nanosleep(clockid_t clk, int flags, const struct timespec* req, struct timespec* rem) {
    return -syscall(SYS_clock_nanosleep, clk, flags, req, rem);
}
