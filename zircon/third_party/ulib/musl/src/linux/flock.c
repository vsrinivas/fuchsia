#include <errno.h>
#include <sys/file.h>

#include "libc.h"

int __flock(int fd, int op) {
  errno = ENOSYS;
  return -1;
}

weak_alias(__flock, flock);
