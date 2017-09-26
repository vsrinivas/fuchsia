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
    zx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, void* out);
    zx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_handle_t* out_handle);

    zx_status_t (*map_interrupt)(void* ctx, uint32_t index, zx_handle_t* out_handle);
} platform_device_protocol_ops_t;

typedef struct {
    platform_device_protocol_ops_t* ops;
    void* ctx;
} platform_device_protocol_t;

// Requests a given protocol from the platform bus implementation
static inline zx_status_t pdev_get_protocol(platform_device_protocol_t* pdev, uint32_t proto_id,
                                             void* out_proto) {
    return pdev->ops->get_protocol(pdev->ctx, proto_id, out_proto);
}

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

// MMIO mapping helpers

typedef struct {
    void*       vaddr;
    size_t      size;
    zx_handle_t handle;
} pdev_mmio_buffer_t;

static inline zx_status_t pdev_map_mmio_buffer(platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, pdev_mmio_buffer_t* buffer) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, &buffer->vaddr, &buffer->size,
                               &buffer->handle);
}

static inline void pdev_mmio_buffer_release(pdev_mmio_buffer_t* buffer) {
    if (buffer->vaddr) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buffer->vaddr, buffer->size);
    }
    zx_handle_close(buffer->handle);
}

__END_CDECLS;
