// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    zx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_handle_t* out_handle);
    zx_status_t (*map_interrupt)(void* ctx, uint32_t index, zx_handle_t* out_handle);
    zx_status_t (*alloc_contig_vmo)(void* ctx, size_t size, uint32_t align_log2,
                                    zx_handle_t* out_handle);
    zx_status_t (*map_contig_vmo)(void* ctx, size_t size, uint32_t align_log2, uint32_t map_flags,
                                  void** out_vaddr, zx_paddr_t* out_paddr, zx_handle_t* out_handle);
} platform_device_protocol_ops_t;

typedef struct {
    platform_device_protocol_ops_t* ops;
    void* ctx;
} platform_device_protocol_t;

// Maps an MMIO region based on information in the MDI
// index is based on ordering of the device's mmio nodes in the MDI
static inline zx_status_t pdev_map_mmio(platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                                        zx_handle_t* out_handle) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, out_vaddr, out_size, out_handle);
}

// Returns an interrupt handle for an IRQ based on information in the MDI
// index is based on ordering of the device's irq nodes in the MDI
static inline zx_status_t pdev_map_interrupt(platform_device_protocol_t* pdev, uint32_t index,
                                             zx_handle_t* out_handle) {
    return pdev->ops->map_interrupt(pdev->ctx, index, out_handle);
}

static inline zx_status_t pdev_alloc_contig_vmo(platform_device_protocol_t* pdev, size_t size,
                                                uint32_t align_log2, zx_handle_t* out_handle) {
    return pdev->ops->alloc_contig_vmo(pdev->ctx, size, align_log2, out_handle);
}

static inline zx_status_t pdev_map_contig_vmo(platform_device_protocol_t* pdev, size_t size,
                                                uint32_t align_log2, uint32_t map_flags,
                                                void** out_vaddr, zx_paddr_t* out_paddr,
                                                zx_handle_t* out_handle) {
    return pdev->ops->map_contig_vmo(pdev->ctx, size, align_log2, map_flags, out_vaddr, out_paddr,
                                     out_handle);
}

// MMIO and contiguous VMO mapping helpers

typedef struct {
    void*       vaddr;
    zx_paddr_t  paddr;   // only used for pdev_map_contig_buffer()
    size_t      size;
    zx_handle_t handle;
} pdev_vmo_buffer_t;

static inline zx_status_t pdev_map_mmio_buffer(platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, pdev_vmo_buffer_t* buffer) {
    return pdev_map_mmio(pdev, index, cache_policy, &buffer->vaddr, &buffer->size, &buffer->handle);
}

static inline zx_status_t pdev_map_contig_buffer(platform_device_protocol_t* pdev, size_t size,
                                                 uint32_t align_log2, uint32_t map_flags,
                                                 pdev_vmo_buffer_t* buffer) {
    return pdev_map_contig_vmo(pdev, size, align_log2, map_flags, &buffer->vaddr, &buffer->paddr,
                               &buffer->handle);
}

static inline zx_status_t pdev_vmo_buffer_cache_op(pdev_vmo_buffer_t* buffer, uint32_t op,
                                                   zx_off_t offset, size_t size) {
    return zx_vmo_op_range(buffer->handle, op, offset, size, NULL, 0);
}

static inline void pdev_vmo_buffer_release(pdev_vmo_buffer_t* buffer) {
    if (buffer->vaddr) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buffer->vaddr, buffer->size);
    }
    zx_handle_close(buffer->handle);
}

__END_CDECLS;
