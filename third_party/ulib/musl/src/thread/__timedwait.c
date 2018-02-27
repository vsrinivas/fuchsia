#include "threads_impl.h"
#include "time_conversion.h"
#include <errno.h>
#include <zircon/syscalls.h>
#include <time.h>

int __timedwait(atomic_int* futex, int val, clockid_t clk, const struct timespec* at) {
    zx_time_t deadline = ZX_TIME_INFINITE;

    if (at) {
        int ret = __timespec_to_deadline(at, clk, &deadline);
        if (ret)
            return ret;
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
