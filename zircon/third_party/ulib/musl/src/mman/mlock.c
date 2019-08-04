#include <errno.h>
#include <sys/mman.h>

int mlock(const void* addr, size_t len) {
  errno = ENOSYS;
  return -1;
}
