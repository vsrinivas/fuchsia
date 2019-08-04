#include <errno.h>
#include <sys/times.h>

clock_t times(struct tms* tms) {
  errno = ENOSYS;
  return -1;
}
