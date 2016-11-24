#include "libc.h"
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include <magenta/syscalls.h>

#define NS_PER_S (1000000000ull)

int __clock_gettime(clockid_t clk, struct timespec* ts) {
    uint32_t mx_clock;
    switch (clk) {
    case CLOCK_MONOTONIC:
        mx_clock = MX_CLOCK_MONOTONIC;
        break;
    case CLOCK_REALTIME:
        mx_clock = MX_CLOCK_UTC;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    mx_time_t now = _mx_time_get(mx_clock);
    ts->tv_sec = now / NS_PER_S;
    ts->tv_nsec = now % NS_PER_S;
    return 0;
}

weak_alias(__clock_gettime, clock_gettime);
