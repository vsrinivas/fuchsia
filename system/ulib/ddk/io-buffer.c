// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <ddk/driver.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <limits.h>
#include <stdio.h>

static mx_status_t io_buffer_init_common(io_buffer_t* buffer, mx_handle_t vmo_handle, size_t size,
                                         mx_off_t offset, uint32_t flags) {
    mx_vaddr_t virt;

    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_handle, 0, size, flags, &virt);
    if (status != MX_OK) {
        printf("io_buffer: mx_vmar_map failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    mx_paddr_t phys;
    size_t lookup_size = size < PAGE_SIZE ? size : PAGE_SIZE;
    status = mx_vmo_op_range(vmo_handle, MX_VMO_OP_LOOKUP, 0, lookup_size, &phys, sizeof(phys));
    if (status != MX_OK) {
        printf("io_buffer: mx_vmo_op_range failed %d size: %zu\n", status, size);
        mx_vmar_unmap(mx_vmar_root_self(), virt, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = offset;
    buffer->virt = (void *)virt;
    buffer->phys = phys;
    return MX_OK;
}

mx_status_t io_buffer_init_aligned(io_buffer_t* buffer, size_t size, uint32_t alignment_log2, uint32_t flags) {
    if (size == 0) {
        return MX_ERR_INVALID_ARGS;
    }
    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_handle_t vmo_handle;
    mx_status_t status = mx_vmo_create_contiguous(get_root_resource(), size, alignment_log2, &vmo_handle);
    if (status != MX_OK) {
        printf("io_buffer: mx_vmo_create_contiguous failed %d\n", status);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, 0, flags);
}

mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags) {
    // A zero alignment gets interpreted as PAGE_SIZE_SHIFT.
    return io_buffer_init_aligned(buffer, size, 0, flags);
}

mx_status_t io_buffer_init_vmo(io_buffer_t* buffer, mx_handle_t vmo_handle, mx_off_t offset,
                               uint32_t flags) {
    uint64_t size;

    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_status_t status = mx_handle_duplicate(vmo_handle, MX_RIGHT_SAME_RIGHTS, &vmo_handle);
    if (status != MX_OK) return status;

    status = mx_vmo_get_size(vmo_handle, &size);
    if (status != MX_OK) {
        mx_handle_close(vmo_handle);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, offset, flags);
}

mx_status_t io_buffer_init_physical(io_buffer_t* buffer, mx_paddr_t addr, size_t size,
                                    mx_handle_t resource, uint32_t cache_policy) {
    mx_handle_t vmo_handle;
    mx_status_t status = mx_vmo_create_physical(resource, addr, size, &vmo_handle);
    if (status != MX_OK) {
        printf("io_buffer: mx_vmo_create_physical failed %d\n", status);
        return status;
    }

    status = mx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != MX_OK) {
        printf("io_buffer: mx_vmo_set_cache_policy failed %d\n", status);
        mx_handle_close(vmo_handle);
        return status;
    }

    uint32_t flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_MAP_RANGE;
    mx_vaddr_t virt;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_handle, 0, size, flags, &virt);
    if (status != MX_OK) {
        printf("io_buffer: mx_vmar_map failed %d size: %zu\n", status, size);
        mx_handle_close(vmo_handle);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = 0;
    buffer->virt = (void *)virt;
    buffer->phys = addr;
    return MX_OK;
}

void io_buffer_release(io_buffer_t* buffer) {
    if (buffer->vmo_handle) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)buffer->virt, buffer->size);
        mx_handle_close(buffer->vmo_handle);
        buffer->vmo_handle = MX_HANDLE_INVALID;
    }
}

mx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const mx_off_t offset, const size_t size) {
    return mx_vmo_op_range(buffer->vmo_handle, op, offset, size, NULL, 0);
}
