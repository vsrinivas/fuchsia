// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <limits.h>

__BEGIN_CDECLS;

typedef struct {
    uint32_t flags;
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint32_t gpio_count;
    uint32_t i2c_channel_count;
    uint32_t reserved[8];
} pdev_device_info_t;

typedef struct {
    zx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_handle_t* out_handle);
    zx_status_t (*map_interrupt)(void* ctx, uint32_t index, zx_handle_t* out_handle);
    zx_status_t (*alloc_contig_vmo)(void* ctx, size_t size, uint32_t align_log2,
                                    zx_handle_t* out_handle);
    zx_status_t (*map_contig_vmo)(void* ctx, size_t size, uint32_t align_log2, uint32_t map_flags,
                                  void** out_vaddr, zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t (*get_device_info)(void* ctx, pdev_device_info_t* out_info);
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

static inline zx_status_t pdev_get_device_info(platform_device_protocol_t* pdev,
                                               pdev_device_info_t* out_info) {
    return pdev->ops->get_device_info(pdev->ctx, out_info);
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
    zx_status_t status = pdev_map_contig_vmo(pdev, size, align_log2, map_flags, &buffer->vaddr,
                                             &buffer->paddr, &buffer->handle);
    if (status == ZX_OK) {
        status = zx_vmo_get_size(buffer->handle, &buffer->size);
    }
    return status;
}

static inline zx_status_t pdev_vmo_buffer_cache_flush(pdev_vmo_buffer_t* buffer, zx_off_t offset,
                                                      size_t length) {
    return zx_cache_flush((uint8_t *)buffer->vaddr + offset, length, ZX_CACHE_FLUSH_DATA);
}

static inline zx_status_t pdev_vmo_buffer_cache_flush_invalidate(pdev_vmo_buffer_t* buffer,
                                                                 zx_off_t offset, size_t length) {
    return zx_cache_flush((uint8_t *)buffer->vaddr + offset, length,
                          ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
}

static inline void pdev_vmo_buffer_release(pdev_vmo_buffer_t* buffer) {
    if (buffer->vaddr) {
        uintptr_t vaddr = ROUNDDOWN((uintptr_t)buffer->vaddr, PAGE_SIZE);
        size_t size;
        zx_vmo_get_size(buffer->handle, &size);
        zx_vmar_unmap(zx_vmar_root_self(), vaddr, size);
    }
    zx_handle_close(buffer->handle);
}

__END_CDECLS;
