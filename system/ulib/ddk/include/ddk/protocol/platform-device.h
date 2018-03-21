// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/serial.h>
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
    serial_port_info_t serial_port_info;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint32_t gpio_count;
    uint32_t i2c_channel_count;
    uint32_t clk_count;
    uint32_t bti_count;
    uint32_t reserved[8];
} pdev_device_info_t;

typedef struct {
    zx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_handle_t* out_handle);
    zx_status_t (*map_interrupt)(void* ctx, uint32_t index, zx_handle_t* out_handle);
    zx_status_t (*get_bti)(void* ctx, uint32_t index, zx_handle_t* out_handle);
    zx_status_t (*get_device_info)(void* ctx, pdev_device_info_t* out_info);
} platform_device_protocol_ops_t;

typedef struct {
    platform_device_protocol_ops_t* ops;
    void* ctx;
} platform_device_protocol_t;

// Maps an MMIO region. "index" is relative to the list of MMIOs for the device.
static inline zx_status_t pdev_map_mmio(platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                                        zx_handle_t* out_handle) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, out_vaddr, out_size, out_handle);
}

// Returns an interrupt handle. "index" is relative to the list of IRQs for the device.
static inline zx_status_t pdev_map_interrupt(platform_device_protocol_t* pdev, uint32_t index,
                                             zx_handle_t* out_handle) {
    return pdev->ops->map_interrupt(pdev->ctx, index, out_handle);
}

// Returns an IOMMU bus transaction initiator handle.
// "index" is relative to the list of BTIs for the device.
static inline zx_status_t pdev_get_bti(platform_device_protocol_t* pdev, uint32_t index,
                                       zx_handle_t* out_handle) {
    return pdev->ops->get_bti(pdev->ctx, index, out_handle);
}

static inline zx_status_t pdev_get_device_info(platform_device_protocol_t* pdev,
                                               pdev_device_info_t* out_info) {
    return pdev->ops->get_device_info(pdev->ctx, out_info);
}

// MMIO mapping helpers

typedef struct {
    void*       vaddr;
    size_t      size;
    zx_handle_t handle;
} pdev_vmo_buffer_t;

static inline zx_status_t pdev_map_mmio_buffer(platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, pdev_vmo_buffer_t* buffer) {
    return pdev_map_mmio(pdev, index, cache_policy, &buffer->vaddr, &buffer->size, &buffer->handle);
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
