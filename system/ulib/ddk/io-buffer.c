// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

// Returns true if a buffer with these parameters was allocated using
// zx_vmo_create_contiguous.  This is primarily important so we know whether we
// need to call COMMIT on it to get the pages to exist.
static bool is_allocated_contiguous(size_t size, uint32_t flags) {
    return (flags & IO_BUFFER_CONTIG) && size > PAGE_SIZE;
}

static zx_status_t io_buffer_init_common(io_buffer_t* buffer, zx_handle_t vmo_handle, size_t size,
                                         zx_off_t offset, uint32_t flags) {
    zx_vaddr_t virt;

    if (!is_allocated_contiguous(size, flags)) {
        // needs to be done before ZX_VMO_OP_LOOKUP for non-contiguous VMOs
        zx_status_t status = zx_vmo_op_range(vmo_handle, ZX_VMO_OP_COMMIT, 0, size, NULL, 0);
        if (status != ZX_OK) {
            zxlogf(ERROR, "io_buffer: zx_vmo_op_range(ZX_VMO_OP_COMMIT) failed %d\n", status);
            zx_handle_close(vmo_handle);
            return status;
        }
    }

    uint32_t map_flags = ZX_VM_FLAG_PERM_READ;
    if (flags & IO_BUFFER_RW) {
        map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    }

    zx_status_t status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, size, map_flags, &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "io_buffer: zx_vmar_map failed %d size: %zu\n", status, size);
        zx_handle_close(vmo_handle);
        return status;
    }

    zx_paddr_t phys = IO_BUFFER_INVALID_PHYS;
    // For contiguous buffers, pre-lookup the physical mapping so
    // io_buffer_phys() works.  For non-contiguous buffers, io_buffer_physmap()
    // will need to be called.
    if (flags & IO_BUFFER_CONTIG) {
        size_t lookup_size = size < PAGE_SIZE ? size : PAGE_SIZE;
        status = zx_vmo_op_range(vmo_handle, ZX_VMO_OP_LOOKUP, 0, lookup_size, &phys, sizeof(phys));
        if (status != ZX_OK) {
            zxlogf(ERROR, "io_buffer: zx_vmo_op_range failed %d size: %zu\n", status, size);
            zx_vmar_unmap(zx_vmar_root_self(), virt, size);
            zx_handle_close(vmo_handle);
            return status;
        }
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = offset;
    buffer->virt = (void *)virt;
    buffer->phys = phys;
    buffer->phys_list = NULL;
    buffer->phys_count = 0;

    return ZX_OK;
}

zx_status_t io_buffer_init_aligned(io_buffer_t* buffer, size_t size, uint32_t alignment_log2, uint32_t flags) {
    buffer->vmo_handle = ZX_HANDLE_INVALID;
    buffer->phys_list = NULL;

    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (flags & ~IO_BUFFER_FLAGS_MASK) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t vmo_handle;
    zx_status_t status;

    if (is_allocated_contiguous(size, flags)) {
        status = zx_vmo_create_contiguous(get_root_resource(), size, alignment_log2, &vmo_handle);
    } else {
        // zx_vmo_create doesn't support passing an alignment.
        if (alignment_log2 != 0)
            return ZX_ERR_INVALID_ARGS;
        status = zx_vmo_create(size, 0, &vmo_handle);
    }
    if (status != ZX_OK) {
        zxlogf(ERROR, "io_buffer: zx_vmo_create failed %d\n", status);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, 0, flags);
}

zx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags) {
    // A zero alignment gets interpreted as PAGE_SIZE_SHIFT.
    return io_buffer_init_aligned(buffer, size, 0, flags);
}

zx_status_t io_buffer_init_vmo(io_buffer_t* buffer, zx_handle_t vmo_handle, zx_off_t offset,
                               uint32_t flags) {
    buffer->vmo_handle = ZX_HANDLE_INVALID;
    buffer->phys_list = NULL;

    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = zx_handle_duplicate(vmo_handle, ZX_RIGHT_SAME_RIGHTS, &vmo_handle);
    if (status != ZX_OK) return status;

    uint64_t size;
    status = zx_vmo_get_size(vmo_handle, &size);
    if (status != ZX_OK) {
        zx_handle_close(vmo_handle);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, offset, flags);
}

zx_status_t io_buffer_init_physical(io_buffer_t* buffer, zx_paddr_t addr, size_t size,
                                    zx_handle_t resource, uint32_t cache_policy) {
    zx_handle_t vmo_handle;
    zx_status_t status = zx_vmo_create_physical(resource, addr, size, &vmo_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "io_buffer: zx_vmo_create_physical failed %d\n", status);
        return status;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "io_buffer: zx_vmo_set_cache_policy failed %d\n", status);
        zx_handle_close(vmo_handle);
        return status;
    }

    uint32_t flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE;
    zx_vaddr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, size, flags, &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "io_buffer: zx_vmar_map failed %d size: %zu\n", status, size);
        zx_handle_close(vmo_handle);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = 0;
    buffer->virt = (void *)virt;
    buffer->phys = addr;
    buffer->phys_list = NULL;
    buffer->phys_count = 0;
    return ZX_OK;
}

void io_buffer_release(io_buffer_t* buffer) {
    if (buffer->vmo_handle != ZX_HANDLE_INVALID) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buffer->virt, buffer->size);
        zx_handle_close(buffer->vmo_handle);
        buffer->vmo_handle = ZX_HANDLE_INVALID;
    }
    free(buffer->phys_list);
    buffer->phys_list = NULL;
    buffer->phys_count = 0;
}

zx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const zx_off_t offset, const size_t size) {
    if (size > 0) {
        return zx_vmo_op_range(buffer->vmo_handle, op, buffer->offset + offset, size, NULL, 0);
    } else {
        return ZX_OK;
    }
}

zx_status_t io_buffer_cache_flush(io_buffer_t* buffer, zx_off_t offset, size_t length) {
    if (offset + length < offset || offset + length > buffer->size) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_cache_flush(io_buffer_virt(buffer) + offset, length, ZX_CACHE_FLUSH_DATA);
}

zx_status_t io_buffer_cache_flush_invalidate(io_buffer_t* buffer, zx_off_t offset, size_t length) {
    if (offset + length < offset || offset + length > buffer->size) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_cache_flush(io_buffer_virt(buffer) + offset, length,
                          ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
}

zx_status_t io_buffer_physmap(io_buffer_t* buffer) {
    if (buffer->phys_count > 0) {
        return ZX_OK;
    }
    if (buffer->size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    // ZX_VMO_OP_LOOKUP returns whole pages, so take into account unaligned vmo
    // offset and length when calculating the amount of pages returned
    uint64_t page_offset = ROUNDDOWN(buffer->offset, PAGE_SIZE);
    // The buffer size is the vmo size from offset 0.
    uint64_t page_length = buffer->size - page_offset;
    uint64_t pages = ROUNDUP(page_length, PAGE_SIZE) / PAGE_SIZE;

    zx_paddr_t* paddrs = malloc(pages * sizeof(zx_paddr_t));
    if (paddrs == NULL) {
        zxlogf(ERROR, "io_buffer: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = io_buffer_physmap_range(buffer, page_offset, page_length,
                                                 pages, paddrs);

    if (status != ZX_OK) {
        free(paddrs);
        return status;
    }
    buffer->phys_list = paddrs;
    buffer->phys_count = pages;
    return ZX_OK;
}
