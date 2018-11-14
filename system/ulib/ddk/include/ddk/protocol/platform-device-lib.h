// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/platform/device.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;


static inline zx_status_t pdev_map_mmio2(const pdev_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                                        zx_paddr_t* out_paddr, zx_handle_t* out_handle) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, out_vaddr, out_size, out_paddr,
                               out_handle);
}

// Returns an interrupt handle. "index" is relative to the list of IRQs for the device.
static inline zx_status_t pdev_map_interrupt(const pdev_protocol_t* pdev, uint32_t index,
                                             zx_handle_t* out_handle) {
    return pdev->ops->get_interrupt(pdev->ctx, index, 0, out_handle);
}

// MMIO mapping helper.
static inline zx_status_t pdev_map_mmio_buffer(const pdev_protocol_t* pdev,
                                               uint32_t index, uint32_t cache_policy,
                                               io_buffer_t* buffer) {
    void* vaddr;
    size_t size;
    zx_paddr_t paddr;
    zx_handle_t vmo_handle;

    zx_status_t status = pdev_map_mmio2(pdev, index, cache_policy, &vaddr, &size, &paddr,
                                        &vmo_handle);
    if (status != ZX_OK) {
        return status;
    }
    zx_off_t offset = (uintptr_t)vaddr & (PAGE_SIZE - 1);
    vaddr = (void *)((uintptr_t)vaddr - offset);
    status = io_buffer_init_mmio(buffer, vmo_handle, vaddr, offset, size);
    if (status == ZX_OK) {
        buffer->phys = paddr;
    }
    zx_handle_close(vmo_handle);
    return status;
}

static inline zx_status_t pdev_map_mmio_buffer2(const pdev_protocol_t* pdev,
                                               uint32_t index, uint32_t cache_policy,
                                               mmio_buffer_t* buffer) {
    pdev_mmio_t mmio;

    zx_status_t status = pdev_get_mmio(pdev, index, &mmio);
    if (status != ZX_OK) {
        return status;
    }
    return mmio_buffer_init(buffer, mmio.offset, mmio.size, mmio.vmo, cache_policy);
}

__END_CDECLS;
