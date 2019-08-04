#include <errno.h>
#include <sys/mman.h>

int munlockall(void) {
  errno = ENOSYS;
  return -1;
}
