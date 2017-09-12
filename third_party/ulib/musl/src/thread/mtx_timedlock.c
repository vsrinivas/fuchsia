#include <zircon/syscalls.h>
#include <runtime/mutex.h>
#include <threads.h>

#include "time_conversion.h"

int mtx_timedlock(mtx_t* restrict m, const struct timespec* restrict ts) {
    zx_time_t abstime = __timespec_to_zx_time_t(*ts);
    zx_status_t status = __zxr_mutex_timedlock((zxr_mutex_t*)&m->__i, abstime);
    switch (status) {
    default:
        return thrd_error;
    case 0:
        return thrd_success;
    case ZX_ERR_TIMED_OUT:
        return thrd_timedout;
    }
}
