#include <errno.h>
#include <sys/mman.h>

#include "libc.h"

int __memfd_create(const char* name, unsigned int flags) {
  errno = ENOSYS;
  return -1;
}

weak_alias(__memfd_create, memfd_create);
