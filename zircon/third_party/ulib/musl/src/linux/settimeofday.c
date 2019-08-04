#define _BSD_SOURCE
#include <errno.h>
#include <sys/time.h>

int settimeofday(const struct timeval* tv, const struct timezone* tz) {
  errno = ENOSYS;
  return -1;
}
