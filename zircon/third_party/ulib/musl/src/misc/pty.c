#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "libc.h"

int posix_openpt(int flags) { return open("/dev/ptmx", flags); }

int grantpt(int fd) { return 0; }

int unlockpt(int fd) {
  int unlock = 0;
  return ioctl(fd, TIOCSPTLCK, &unlock);
}

int __ptsname_r(int fd, char* buf, size_t len) {
  // TODO(kulakowski) terminal handling.
  return ENOSYS;
}

weak_alias(__ptsname_r, ptsname_r);
