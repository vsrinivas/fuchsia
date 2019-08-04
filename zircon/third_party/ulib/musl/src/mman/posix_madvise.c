#define _GNU_SOURCE
#include <errno.h>
#include <sys/mman.h>

int posix_madvise(void* addr, size_t len, int advice) {
  if (advice == MADV_DONTNEED)
    return 0;
  errno = ENOSYS;
  return -1;
}
