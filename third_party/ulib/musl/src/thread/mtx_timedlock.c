#include <magenta/syscalls.h>
#include <runtime/mutex.h>
#include <threads.h>

#include "time_conversion.h"

int mtx_timedlock(mtx_t* restrict m, const struct timespec* restrict ts) {
    mx_time_t abstime = __timespec_to_mx_time_t(*ts);
    mx_status_t status = __mxr_mutex_timedlock((mxr_mutex_t*)&m->__i, abstime);
    switch (status) {
    default:
        return thrd_error;
    case 0:
        return thrd_success;
    case MX_ERR_TIMED_OUT:
        return thrd_timedout;
    }
}
