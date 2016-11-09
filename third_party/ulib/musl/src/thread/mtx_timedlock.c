#include <magenta/syscalls.h>
#include <runtime/mutex.h>
#include <threads.h>

#include "time_conversion.h"

int mtx_timedlock(mtx_t* restrict m, const struct timespec* restrict ts) {
    mx_time_t timeout = __timespec_to_mx_time_t(*ts);
    // TODO(kulakowski) Use MX_CLOCK_UTC when available.
    mx_time_t now = _mx_time_get(MX_CLOCK_MONOTONIC);
    if (timeout < now) {
        return thrd_timedout;
    }
    mx_time_t relative = timeout - now;
    mx_status_t status = mxr_mutex_timedlock((mxr_mutex_t*)&m->__i, relative);
    switch (status) {
    default:
        return thrd_error;
    case 0:
        return thrd_success;
    case ERR_TIMED_OUT:
        return thrd_timedout;
    }
}
