#include <errno.h>
#include <sys/time.h>

int getitimer(int which, struct itimerval* old) {
  errno = ENOSYS;
  return -1;
}
