#include <errno.h>
#include <sys/mman.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "libc.h"
#include "zircon_impl.h"

int __mprotect(void* addr, size_t len, int prot) {
  uintptr_t ptr = (uintptr_t)addr;
  zx_vm_option_t zx_prot = 0;
  zx_prot |= (prot & PROT_READ) ? ZX_VM_PERM_READ : 0;
  zx_prot |= (prot & PROT_WRITE) ? ZX_VM_PERM_WRITE : 0;
  zx_prot |= (prot & PROT_EXEC) ? ZX_VM_PERM_EXECUTE : 0;
  zx_status_t status = _zx_vmar_protect(_zx_vmar_root_self(), zx_prot, ptr, len);
  if (!status)
    return 0;

  switch (status) {
    case ZX_ERR_ACCESS_DENIED:
      errno = EACCES;
      break;
    case ZX_ERR_INVALID_ARGS:
      errno = ENOTSUP;
      break;
    default:
      errno = EINVAL;
      break;
  }
  return -1;
}

weak_alias(__mprotect, mprotect);
