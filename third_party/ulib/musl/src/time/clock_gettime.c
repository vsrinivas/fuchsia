#include "libc.h"
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "clock_impl.h"

#include <magenta/syscalls.h>

int __clock_gettime(clockid_t clk, struct timespec* ts) {
    uint32_t mx_clock;
    switch (clk) {
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
        mx_clock = MX_CLOCK_MONOTONIC;
        break;
    case CLOCK_REALTIME:
        mx_clock = MX_CLOCK_UTC;
        break;
    case CLOCK_THREAD_CPUTIME_ID:
        mx_clock = MX_CLOCK_THREAD;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    mx_time_t now = _mx_time_get(mx_clock);
    ts->tv_sec = now / MX_SEC(1);
    ts->tv_nsec = now % MX_SEC(1);
    return 0;
}

weak_alias(__clock_gettime, clock_gettime);
