#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>

int sethostname(const char* name, size_t len) {
  errno = ENOSYS;
  return -1;
}
