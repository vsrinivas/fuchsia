#include <errno.h>
#include <sys/file.h>

int flock(int fd, int op) {
  errno = ENOSYS;
  return -1;
}
