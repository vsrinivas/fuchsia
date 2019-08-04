#include <errno.h>
#include <time.h>

int clock_nanosleep(clockid_t clk, int flags, const struct timespec* req, struct timespec* rem) {
  return ENOSYS;
}
