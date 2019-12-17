#include <errno.h>
#include <time.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/clock.h>
#include <zircon/utc.h>

#include "libc.h"

int __clock_settime(clockid_t clk, const struct timespec* ts) {
  switch (clk) {
    // The only clock that might be settable is CLOCK_REALTIME (and even then,
    // you almost certainly do not have the permission to do so).  All of the
    // other clocks cannot be set.
    case CLOCK_REALTIME:
      break;

    case CLOCK_BOOTTIME:
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_THREAD_CPUTIME_ID:
      errno = EPERM;
      return -1;

    default:
      errno = EINVAL;
      return -1;
  }

  // Borrow the clock handle (if any) from the runtime.  If there is no UTC
  // clock handle available, tell the user that they don't have permission to
  // set the clock.
  zx_handle_t utc_clock = _zx_utc_reference_get();
  if (utc_clock == ZX_HANDLE_INVALID) {
    errno = EPERM;
    return -1;
  }

  // Go ahead and attempt to update the clock now.
  zx_clock_update_args_v1_t args = {.value =
                                        ((int64_t)ts->tv_sec * ZX_SEC(1)) + ((int64_t)ts->tv_nsec)};
  zx_status_t status = _zx_clock_update(
      utc_clock, ZX_CLOCK_ARGS_VERSION(1) | ZX_CLOCK_UPDATE_OPTION_VALUE_VALID, &args);

  switch (status) {
    // If we succeeded, just get out.
    case ZX_OK:
      return 0;

    // If we got ZX_ERR_ACCESS_DENIED (we almost certainly will), then we didn't
    // have WRITE access to the clock via our handle.
    case ZX_ERR_ACCESS_DENIED:
      errno = EPERM;
      return -1;

    // If we got ZX_ERR_INVALID_ARGS, then we attempted to apply a clock value
    // which is incompatible with the clock itself.  Unless we are in a strange
    // test environment, this is going to be because someone attempted to roll
    // UTC back to a value before the configured backstop.
    case ZX_ERR_INVALID_ARGS:
      errno = EINVAL;
      return -1;

    default:
      // The only other possible error is that the handle was bad, which should be
      // a panic.  Clock handles are checked by libc when they are installed, so
      // they should never be bad (unless someone broke the rules about closing a
      // clock handle borrowed from libc)
      __builtin_trap();
  }
}

weak_alias(__clock_settime, clock_settime);
