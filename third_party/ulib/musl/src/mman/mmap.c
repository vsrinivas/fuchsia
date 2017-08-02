#include <errno.h>
#include <limits.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "magenta_impl.h"
#include "pthread_impl.h"
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

    // build magenta flags for this
    uint32_t mx_flags = 0;
    mx_flags |= (prot & PROT_READ) ? MX_VM_FLAG_PERM_READ : 0;
    mx_flags |= (prot & PROT_WRITE) ? MX_VM_FLAG_PERM_WRITE : 0;
    mx_flags |= (prot & PROT_EXEC) ? MX_VM_FLAG_PERM_EXECUTE : 0;

    size_t offset = 0;
    mx_status_t status = MX_OK;
    if (flags & MAP_FIXED) {
        mx_flags |= MX_VM_FLAG_SPECIFIC;

        mx_info_vmar_t info;
        status = _mx_object_get_info(_mx_vmar_root_self(), MX_INFO_VMAR, &info,
                                     sizeof(info), NULL, NULL);
        if (status < 0 || (uintptr_t)start < info.base) {
            goto fail;
        }
        offset = (uintptr_t)start - info.base;
    }

    mx_handle_t vmo;
    uintptr_t ptr = 0;
    if (flags & MAP_ANON) {
        if (_mx_vmo_create(len, 0, &vmo) < 0) {
            errno = ENOMEM;
            return MAP_FAILED;
        }
        _mx_object_set_property(vmo, MX_PROP_NAME, mmap_vmo_name, strlen(mmap_vmo_name));
    } else {
        status = _mmap_file(offset, len, mx_flags, flags, fd, fd_off, &ptr);
        if (status < 0) {
            goto fail;
        }
        return (void*) ptr;
    }

    status = _mx_vmar_map(_mx_vmar_root_self(), offset, vmo, fd_off, len, mx_flags, &ptr);
    _mx_handle_close(vmo);
    // TODO: map this as shared if we ever implement forking
    if (status < 0) {
        goto fail;
    }

    return (void*)ptr;

fail:
    switch(status) {
    case MX_ERR_BAD_HANDLE:
        errno = EBADF;
        break;
    case MX_ERR_NOT_SUPPORTED:
        errno = ENODEV;
        break;
    case MX_ERR_ACCESS_DENIED:
        errno = EACCES;
        break;
    case MX_ERR_NO_MEMORY:
        errno = ENOMEM;
        break;
    case MX_ERR_INVALID_ARGS:
    case MX_ERR_BAD_STATE:
    default:
        errno = EINVAL;
    }
    return MAP_FAILED;
}

weak_alias(__mmap, mmap);
