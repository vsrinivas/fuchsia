#pragma once

#define __NEED_struct_timespec

#include <zircon/time.h>
#include <zircon/types.h>

#include <bits/alltypes.h>

#include "threads_impl.h"

static inline zx_time_t __duration_timespec_to_deadline(const struct timespec timespec) {
  zx_duration_t nanos = zx_duration_add_duration(
      zx_duration_mul_int64(1000000000ll, timespec.tv_sec), timespec.tv_nsec);
  return _zx_deadline_after(nanos);
}

static inline int __timespec_to_deadline(const struct timespec* timespec, clockid_t clk,
                                         zx_time_t* deadline) {
  struct timespec to;

  if (timespec->tv_nsec >= ZX_SEC(1))
    return EINVAL;
  if (__clock_gettime(clk, &to))
    return EINVAL;
  to.tv_sec = timespec->tv_sec - to.tv_sec;
  if ((to.tv_nsec = timespec->tv_nsec - to.tv_nsec) < 0) {
    to.tv_sec--;
    to.tv_nsec += ZX_SEC(1);
  }
  if (to.tv_sec < 0)
    return ETIMEDOUT;
  *deadline = __duration_timespec_to_deadline(to);
  return 0;
}
