#include "libc.h"
#include "magenta_impl.h"
#include <errno.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <sys/mman.h>

int __mprotect(void* addr, size_t len, int prot) {
    uintptr_t ptr = (uintptr_t)addr;
    uint32_t mx_prot = 0;
    mx_prot |= (prot & PROT_READ) ? MX_VM_FLAG_PERM_READ : 0;
    mx_prot |= (prot & PROT_WRITE) ? MX_VM_FLAG_PERM_WRITE : 0;
    mx_prot |= (prot & PROT_EXEC) ? MX_VM_FLAG_PERM_EXECUTE : 0;
    mx_status_t status = _mx_vmar_protect(_mx_vmar_root_self(), ptr, len, mx_prot);
    if (!status)
        return 0;

    switch (status) {
    case MX_ERR_ACCESS_DENIED:
        errno = EACCES;
        break;
    case MX_ERR_INVALID_ARGS:
        errno = ENOTSUP;
        break;
    default:
        errno = EINVAL;
        break;
    }
    return -1;
}

weak_alias(__mprotect, mprotect);
