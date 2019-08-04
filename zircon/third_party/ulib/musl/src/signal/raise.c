#include <errno.h>
#include <signal.h>

int raise(int sig) {
  errno = ENOSYS;
  return -1;
}
