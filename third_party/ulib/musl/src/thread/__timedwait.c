#include "pthread_impl.h"
#include <errno.h>
#include <magenta/syscalls.h>
#include <time.h>

#include "clock_impl.h"

int __timedwait(atomic_int* futex, int val, clockid_t clk, const struct timespec* at) {
    struct timespec to;
    mx_time_t deadline = MX_TIME_INFINITE;

    if (at) {
        if (at->tv_nsec >= MX_SEC(1))
            return EINVAL;
        if (__clock_gettime(clk, &to))
            return EINVAL;
        to.tv_sec = at->tv_sec - to.tv_sec;
        if ((to.tv_nsec = at->tv_nsec - to.tv_nsec) < 0) {
            to.tv_sec--;
            to.tv_nsec += MX_SEC(1);
        }
        if (to.tv_sec < 0)
            return ETIMEDOUT;
        deadline = _mx_deadline_after(MX_SEC(to.tv_sec) + to.tv_nsec);
    }

    // mx_futex_wait will return MX_ERR_BAD_STATE if someone modifying *addr
    // races with this call. But this is indistinguishable from
    // otherwise being woken up just before someone else changes the
    // value. Therefore this functions returns 0 in that case.
    switch (_mx_futex_wait(futex, val, deadline)) {
    case MX_OK:
    case MX_ERR_BAD_STATE:
        return 0;
    case MX_ERR_TIMED_OUT:
        return ETIMEDOUT;
    case MX_ERR_INVALID_ARGS:
    default:
        __builtin_trap();
    }
}
