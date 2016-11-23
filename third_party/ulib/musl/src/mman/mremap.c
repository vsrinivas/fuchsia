#define _GNU_SOURCE
#include "libc.h"
#include <errno.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void dummy(void) {}
weak_alias(dummy, __vm_wait);

#define ROUND(addr) ((addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

static mx_status_t vmo_remap(uintptr_t old_mapping, size_t old_len, size_t new_len, uint32_t flags, uintptr_t* new_mapping) {
    if (new_len < old_len) {
        // TODO(kulakowski)
        // For now we can't partially unmap.
        *new_mapping = old_mapping;
        return NO_ERROR;
    }

    size_t offset = 0;
    if (flags & MX_VM_FLAG_SPECIFIC) {
        mx_info_vmar_t info;
        mx_status_t status = mx_object_get_info(_mx_vmar_root_self(),
                                                MX_INFO_VMAR, &info,
                                                sizeof(info), NULL, NULL);
        if (status < 0) {
            return status;
        }
        if (*new_mapping < info.base) {
            return ERR_INVALID_ARGS;
        }
        offset = *new_mapping - info.base;
    }

    mx_handle_t vmo;
    mx_status_t status = _mx_vmo_create(new_len, 0, &vmo);
    if (status < 0) {
        return status;
    }

    status = _mx_vmar_map(_mx_vmar_root_self(), offset, vmo, 0u, new_len,
                          flags, new_mapping);
    _mx_handle_close(vmo);
    if (status != NO_ERROR) {
        return status;
    }

    memcpy((void*)*new_mapping, (void*)old_mapping, old_len);

    status = _mx_vmar_unmap(_mx_vmar_root_self(), old_mapping, old_len);
    if (status != NO_ERROR) {
        _mx_vmar_unmap(_mx_vmar_root_self(), *new_mapping, new_len);
        return status;
    }

    return NO_ERROR;
}

// TODO(kulakowski) mremap is a Linux extension. So far as I can see,
// it's used internally be realloc, but nothing else directly in
// Fuchsia. Consider not exposing it.
void* __fake_mremap(void* old_addr, size_t old_len, size_t new_len, int flags, ...) {
    if (flags & MREMAP_FIXED) {
        if (!(flags & MREMAP_MAYMOVE)) {
            // MREMAP_FIXED requires MREMAP_MAYMOVE to also be specified.
            errno = EINVAL;
            return MAP_FAILED;
        }
        // TODO(kulakowski) Support MREMAP_FIXED.
        errno = ENOMEM;
        return MAP_FAILED;
    }

    if (!(flags & MREMAP_MAYMOVE)) {
        // TODO(kulakowski) Support extending ranges.
        errno = ENOMEM;
        return MAP_FAILED;
    }

    uintptr_t mapping = (uintptr_t)old_addr;
    if (ROUND(mapping) != mapping) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (new_len >= PTRDIFF_MAX) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    void* new_addr = NULL;
    if (flags & MREMAP_FIXED) {
        __vm_wait();
        va_list ap;
        va_start(ap, flags);
        new_addr = va_arg(ap, void*);
        va_end(ap);
    }

    // TODO(kulakowski) Right now, the only caller of this is
    // realloc. So give the mapping RW permissions.
    uint32_t mx_flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
    mx_flags |= (flags & MREMAP_FIXED) ? MX_VM_FLAG_SPECIFIC : 0;

    uintptr_t new_mapping = 0u;
    mx_status_t status = vmo_remap(mapping, old_len, new_len, mx_flags, &new_mapping);
    if (status != NO_ERROR) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    return (void*)new_mapping;
}

