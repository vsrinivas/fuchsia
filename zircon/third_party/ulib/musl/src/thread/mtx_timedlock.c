#include <lib/sync/mutex.h>
#include <threads.h>
#include <zircon/syscalls.h>

#include "time_conversion.h"

int mtx_timedlock(mtx_t* restrict m, const struct timespec* restrict ts) {
  zx_time_t deadline = ZX_TIME_INFINITE;
  while (true) {
    int ret = __timespec_to_deadline(ts, CLOCK_REALTIME, &deadline);
    if (ret)
      return ret == ETIMEDOUT ? thrd_timedout : thrd_error;

    zx_status_t status = sync_mutex_timedlock((sync_mutex_t*)&m->__i, deadline);
    switch (status) {
      default:
        return thrd_error;
      case 0:
        return thrd_success;
      case ZX_ERR_TIMED_OUT:
        // Because CLOCK_REALTIME could have been adjusted foward while we were
        // waiting we need to reevaluate the deadline and wait again if we are early.
        // this is the expected behavior of C11 and pthread constructs.
        continue;
    }
  }
}
