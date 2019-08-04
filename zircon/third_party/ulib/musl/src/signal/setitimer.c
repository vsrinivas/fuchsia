#include <errno.h>
#include <sys/time.h>

int setitimer(int which, const struct itimerval* restrict new, struct itimerval* restrict old) {
  errno = ENOSYS;
  return -1;
}
