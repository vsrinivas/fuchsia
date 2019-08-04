#include <errno.h>
#include <sys/mman.h>

int msync(void* start, size_t len, int flags) {
  errno = ENOSYS;
  return -1;
}
