#include <errno.h>
#include <time.h>
#include <zircon/syscalls.h>

#include "libc.h"

int clock_getres(clockid_t clk, struct timespec* ts) {
  switch (clk) {
    case CLOCK_BOOTTIME:
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_REALTIME:
    case CLOCK_THREAD_CPUTIME_ID:
      break;

    default:
      errno = EINVAL;
      return -1;
  }

  // The kernel's ability to measure time is determined by the underlying
  // resolution of the selected source for the tick counter. Despite the fact
  // that the kernel clock APIs all normalize their units to nanoseconds, the
  // underlying resolution is always that of the tick counter reference.
  //
  // When reporting the resolution of any of the posix clocks, report the
  // resolution of the underlying tick counter, at least to the best of our
  // ability.  If the underlying tick reference is ticking faster than 1GHz
  // (unusual, but technically possible) simply report a resolution of 1 nSec.
  uint64_t nsec_per_tick = ZX_SEC(1) / _zx_ticks_per_second();
  if (!nsec_per_tick) {
    nsec_per_tick = 1;
  }

  ts->tv_sec = nsec_per_tick / ZX_SEC(1);
  ts->tv_nsec = nsec_per_tick % ZX_SEC(1);

  return 0;
}
