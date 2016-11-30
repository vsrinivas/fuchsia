// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <ddk/driver.h>
#include <magenta/syscalls.h>
#include <limits.h>
#include <stdio.h>

static mx_status_t io_buffer_init_common(io_buffer_t* buffer, mx_handle_t vmo_handle, size_t size,
                                         mx_off_t offset, uint32_t flags) {
    mx_vaddr_t virt;

    flags |= MX_VM_FLAG_DMA;
    // Temporary hack to ensure our mapping does not conflict with DSO loading
    flags |= MX_VM_FLAG_ALLOC_BASE;

    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_handle, 0, size, flags, &virt);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmar_map failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    mx_paddr_t phys;
    status = mx_vmo_op_range(vmo_handle, MX_VMO_OP_LOOKUP, 0, PAGE_SIZE, &phys, sizeof(phys));
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmo_op_range failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = offset;
    buffer->virt = (void *)virt;
    buffer->phys = phys;
    return NO_ERROR;
}

mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags) {
    if (size == 0) {
        return ERR_INVALID_ARGS;
    }
    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return ERR_INVALID_ARGS;
    }

    mx_handle_t vmo_handle;
    mx_status_t status = mx_vmo_create_contiguous(get_root_resource(), size, &vmo_handle);
    if (status != NO_ERROR) {
        printf("io_buffer: mx_vmo_create failed %d\n", vmo_handle);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, 0, flags);
}

mx_status_t io_buffer_init_vmo(io_buffer_t* buffer, mx_handle_t vmo_handle, mx_off_t offset,
                               uint32_t flags) {
    uint64_t size;

    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t status = mx_handle_duplicate(vmo_handle, MX_RIGHT_SAME_RIGHTS, &vmo_handle);
    if (status != NO_ERROR) return status;

    status = mx_vmo_get_size(vmo_handle, &size);
    if (status != NO_ERROR) return status;

    return io_buffer_init_common(buffer, vmo_handle, size, offset, flags);
}

// copies an io_buffer. clone gets duplicate of the source's vmo_handle
mx_status_t io_buffer_clone(io_buffer_t* src, io_buffer_t* dest) {
    mx_status_t status = mx_handle_duplicate(src->vmo_handle, MX_RIGHT_SAME_RIGHTS,
                                             &dest->vmo_handle);
    if (status < 0) return status;
    dest->size = src->size;
    dest->offset = src->offset;
    dest->virt = src->virt;
    dest->phys = src->phys;
    return NO_ERROR;
}

void io_buffer_release(io_buffer_t* buffer) {
    if (buffer->vmo_handle) {
        mx_handle_close(buffer->vmo_handle);
        buffer->vmo_handle = MX_HANDLE_INVALID;
    }
}
