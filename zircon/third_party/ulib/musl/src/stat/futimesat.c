#define _GNU_SOURCE
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "libc.h"

int __futimesat(int dirfd, const char* pathname, const struct timeval times[2]) {
  struct timespec ts[2];
  if (times) {
    int i;
    for (i = 0; i < 2; i++) {
      if (times[i].tv_usec >= 1000000ULL) {
        errno = EINVAL;
        return -1;
      }
      ts[i].tv_sec = times[i].tv_sec;
      ts[i].tv_nsec = times[i].tv_usec * 1000;
    }
  }
  return utimensat(dirfd, pathname, times ? ts : 0, 0);
}

weak_alias(__futimesat, futimesat);
