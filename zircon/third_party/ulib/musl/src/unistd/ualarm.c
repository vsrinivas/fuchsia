#define _GNU_SOURCE
#include <sys/time.h>
#include <unistd.h>

unsigned ualarm(unsigned value, unsigned interval) {
  struct itimerval it = {
      .it_interval.tv_usec = interval,
      .it_value.tv_usec = value,
  };
  struct itimerval old = {};
  setitimer(ITIMER_REAL, &it, &old);
  return old.it_value.tv_sec * 1000000 + old.it_value.tv_usec;
}
