#include <threads.h>

#include <assert.h>
#include <zircon/syscalls.h>

#include "time_conversion.h"

int thrd_sleep(const struct timespec* req, struct timespec* rem) {
    zx_time_t deadline = ZX_TIME_INFINITE;
    int ret = __timespec_to_deadline(req, CLOCK_REALTIME, &deadline);
    if (ret) {
        // According to the API, failures not due to signals should return a
        // negative value other than -1. So return -2 if we didn't timeout.
        return ret == ETIMEDOUT ? 0 : -2;
    }

    // For now, Zircon only provides an uninterruptible nanosleep. If
    // we ever introduce an asynchronous mechanism that would require
    // some EINTR-like logic, then we will also want a nanosleep call
    // which reports back how much time is remaining. Until then,
    // always report back 0 timeout remaining.

    ret = _zx_nanosleep(deadline);
    assert(ret == 0);
    if (rem) {
        *rem = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0,
        };
    }
    return 0;
}
