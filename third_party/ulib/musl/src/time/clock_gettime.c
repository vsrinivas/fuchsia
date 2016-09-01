#include "libc.h"
#include <stdint.h>
#include <time.h>

#include <magenta/syscalls.h>

#define NS_PER_S (1000000000ull)

int __clock_gettime(clockid_t clk, struct timespec* ts) {
    mx_time_t now = _mx_current_time();
    ts->tv_sec = now / NS_PER_S;
    ts->tv_nsec = now % NS_PER_S;
    return 0;
}

weak_alias(__clock_gettime, clock_gettime);
