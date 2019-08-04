#include <errno.h>
#include <sys/mman.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "libc.h"
#include "zircon_impl.h"

int __munmap(void* start, size_t len) {
  uintptr_t ptr = (uintptr_t)start;
  zx_status_t status = _zx_vmar_unmap(_zx_vmar_root_self(), ptr, len);
  if (status < 0) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

weak_alias(__munmap, munmap);
