#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/utc.h>

#include <runtime/thread.h>

#include "libc.h"
#include "threads_impl.h"

static int gettime_finish(zx_status_t syscall_status, zx_time_t now, struct timespec* ts) {
  if (syscall_status != ZX_OK) {
    __builtin_trap();
  }

  ts->tv_sec = now / ZX_SEC(1);
  ts->tv_nsec = now % ZX_SEC(1);
  return 0;
}

static int gettime_via_get(zx_clock_t clock_id, struct timespec* ts) {
  zx_time_t now;
  zx_status_t syscall_status = _zx_clock_get(clock_id, &now);
  return gettime_finish(syscall_status, now, ts);
}

static int gettime_via_utc(struct timespec* ts) {
  zx_handle_t utc_clock = _zx_utc_reference_get();

  if (utc_clock != ZX_HANDLE_INVALID) {
    // Note: utc_clock is a borrowed handle.  We should not close it.
    zx_time_t now;
    zx_status_t syscall_status = _zx_clock_read(utc_clock, &now);
    return gettime_finish(syscall_status, now, ts);
  } else {
    // TODO(johngro): When UTC time eventually completely leaves the kernel,
    // lack of a UTC reference will result in an ENOTSUP error instead of
    // falling back on the kernel UTC representation.
    return gettime_via_get(ZX_CLOCK_UTC, ts);
  }
}

int __clock_gettime(clockid_t clk, struct timespec* ts) {
  switch (clk) {
    case CLOCK_BOOTTIME:  // see fxbug.dev/38552
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
      return gettime_finish(ZX_OK, _zx_clock_get_monotonic(), ts);

    case CLOCK_REALTIME:
      return gettime_via_utc(ts);

    case CLOCK_THREAD_CPUTIME_ID: {
      zx_info_thread_stats_t info;
      zx_status_t status;

      status = _zx_object_get_info(zxr_thread_get_handle(&__pthread_self()->zxr_thread),
                                   ZX_INFO_THREAD_STATS, &info, sizeof(info), NULL, NULL);
      return gettime_finish(status, info.total_runtime, ts);
    }

    default:
      errno = EINVAL;
      return -1;
  }
}

weak_alias(__clock_gettime, clock_gettime);
