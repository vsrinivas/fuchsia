#include <sys/time.h>
#include <time.h>

int gettimeofday(struct timeval* restrict tv, void* restrict tz) {
  struct timespec ts;
  if (!tv)
    return 0;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    return -1;
  tv->tv_sec = ts.tv_sec;
  tv->tv_usec = (int)ts.tv_nsec / 1000;
  return 0;
}
