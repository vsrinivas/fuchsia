#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>

int acct(const char* filename) {
  errno = ENOSYS;
  return -1;
}
