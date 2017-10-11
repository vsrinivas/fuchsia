#include "pthread_impl.h"
#include <errno.h>
#include <zircon/syscalls.h>
#include <time.h>

int __timedwait(atomic_int* futex, int val, clockid_t clk, const struct timespec* at) {
    struct timespec to;
    zx_time_t deadline = ZX_TIME_INFINITE;

    if (at) {
        if (at->tv_nsec >= ZX_SEC(1))
            return EINVAL;
        if (__clock_gettime(clk, &to))
            return EINVAL;
        to.tv_sec = at->tv_sec - to.tv_sec;
        if ((to.tv_nsec = at->tv_nsec - to.tv_nsec) < 0) {
            to.tv_sec--;
            to.tv_nsec += ZX_SEC(1);
        }
        if (to.tv_sec < 0)
            return ETIMEDOUT;
        deadline = _zx_deadline_after(ZX_SEC(to.tv_sec) + to.tv_nsec);
    }

    // zx_futex_wait will return ZX_ERR_BAD_STATE if someone modifying *addr
    // races with this call. But this is indistinguishable from
    // otherwise being woken up just before someone else changes the
    // value. Therefore this functions returns 0 in that case.
    switch (_zx_futex_wait(futex, val, deadline)) {
    case ZX_OK:
    case ZX_ERR_BAD_STATE:
        return 0;
    case ZX_ERR_TIMED_OUT:
        return ETIMEDOUT;
    case ZX_ERR_INVALID_ARGS:
    default:
        __builtin_trap();
    }
}
