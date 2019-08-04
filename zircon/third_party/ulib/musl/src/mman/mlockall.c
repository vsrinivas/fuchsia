#include <errno.h>
#include <sys/mman.h>

int mlockall(int flags) {
  errno = ENOSYS;
  return -1;
}
