#include <errno.h>
#include <limits.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "zircon_impl.h"
#include "threads_impl.h"
#include "stdio_impl.h"

static const char mmap_vmo_name[] = "mmap-anonymous";

void* __mmap(void* start, size_t len, int prot, int flags, int fd, off_t fd_off) {
    if (fd_off & (PAGE_SIZE - 1)) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (len == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (len >= PTRDIFF_MAX) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    if (!(flags & (MAP_PRIVATE | MAP_SHARED)) ||
        (flags & MAP_PRIVATE && flags & MAP_SHARED)) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    // round up to page size
    len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // build zircon flags for this
    uint32_t zx_flags = 0;
    zx_flags |= (prot & PROT_READ) ? ZX_VM_FLAG_PERM_READ : 0;
    zx_flags |= (prot & PROT_WRITE) ? ZX_VM_FLAG_PERM_WRITE : 0;
    zx_flags |= (prot & PROT_EXEC) ? ZX_VM_FLAG_PERM_EXECUTE : 0;

    size_t offset = 0;
    zx_status_t status = ZX_OK;
    if (flags & MAP_FIXED) {
        zx_flags |= ZX_VM_FLAG_SPECIFIC;

        zx_info_vmar_t info;
        status = _zx_object_get_info(_zx_vmar_root_self(), ZX_INFO_VMAR, &info,
                                     sizeof(info), NULL, NULL);
        if (status < 0 || (uintptr_t)start < info.base) {
            goto fail;
        }
        offset = (uintptr_t)start - info.base;
    }

    zx_handle_t vmo;
    uintptr_t ptr = 0;
    if (flags & MAP_ANON) {
        if (_zx_vmo_create(len, 0, &vmo) < 0) {
            errno = ENOMEM;
            return MAP_FAILED;
        }
        _zx_object_set_property(vmo, ZX_PROP_NAME, mmap_vmo_name, strlen(mmap_vmo_name));
    } else {
        status = _mmap_file(offset, len, zx_flags, flags, fd, fd_off, &ptr);
        if (status < 0) {
            goto fail;
        }
        return (void*) ptr;
    }

    status = _zx_vmar_map(_zx_vmar_root_self(), offset, vmo, fd_off, len, zx_flags, &ptr);
    _zx_handle_close(vmo);
    // TODO: map this as shared if we ever implement forking
    if (status < 0) {
        goto fail;
    }

    return (void*)ptr;

fail:
    switch(status) {
    case ZX_ERR_BAD_HANDLE:
        errno = EBADF;
        break;
    case ZX_ERR_NOT_SUPPORTED:
        errno = ENODEV;
        break;
    case ZX_ERR_ACCESS_DENIED:
        errno = EACCES;
        break;
    case ZX_ERR_NO_MEMORY:
        errno = ENOMEM;
        break;
    case ZX_ERR_INVALID_ARGS:
    case ZX_ERR_BAD_STATE:
    default:
        errno = EINVAL;
    }
    return MAP_FAILED;
}

weak_alias(__mmap, mmap);
