#include "libc.h"
#include "zircon_impl.h"
#include <errno.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <sys/mman.h>

int __mprotect(void* addr, size_t len, int prot) {
    uintptr_t ptr = (uintptr_t)addr;
    uint32_t zx_prot = 0;
    zx_prot |= (prot & PROT_READ) ? ZX_VM_FLAG_PERM_READ : 0;
    zx_prot |= (prot & PROT_WRITE) ? ZX_VM_FLAG_PERM_WRITE : 0;
    zx_prot |= (prot & PROT_EXEC) ? ZX_VM_FLAG_PERM_EXECUTE : 0;
    zx_status_t status = _zx_vmar_protect(_zx_vmar_root_self(), ptr, len, zx_prot);
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
