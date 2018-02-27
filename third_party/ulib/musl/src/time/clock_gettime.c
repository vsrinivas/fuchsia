#include "threads_impl.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

#include <zircon/syscalls.h>

#include "libc.h"

int __clock_gettime(clockid_t clk, struct timespec* ts) {
    uint32_t zx_clock;
    switch (clk) {
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
        zx_clock = ZX_CLOCK_MONOTONIC;
        break;
    case CLOCK_REALTIME:
        zx_clock = ZX_CLOCK_UTC;
        break;
    case CLOCK_THREAD_CPUTIME_ID:
        zx_clock = ZX_CLOCK_THREAD;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    zx_time_t now = _zx_clock_get(zx_clock);
    ts->tv_sec = now / ZX_SEC(1);
    ts->tv_nsec = now % ZX_SEC(1);
    return 0;
}

weak_alias(__clock_gettime, clock_gettime);
