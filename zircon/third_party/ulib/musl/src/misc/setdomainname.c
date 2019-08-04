#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>

int setdomainname(const char* name, size_t len) {
  errno = ENOSYS;
  return -1;
}
