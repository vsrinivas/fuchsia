#define _BSD_SOURCE
#include <errno.h>
#include <sys/time.h>
#include <time.h>

int settimeofday(const struct timeval* tv, const struct timezone* tz) {
  // We absolutely do not allow setting the system timezone via settimeofday.
  if (tz != NULL) {
    errno = ENOTSUP;
    return -1;
  }

  // Sanity check the timeval.  POSIX says that tv->tv_sec must be non-negative,
  // and that tv->tv_usec must be on the range [0, 1000000).
  if ((tv->tv_sec < 0) || (tv->tv_usec < 0) || (tv->tv_usec >= 1000000)) {
    errno = EINVAL;
    return -1;
  }

  // Convert time a timeval structure to a timespec structure and forward the
  // call on to clock_settime.
  struct timespec ts;
  TIMEVAL_TO_TIMESPEC(tv, &ts);
  return clock_settime(CLOCK_REALTIME, &ts);
}
