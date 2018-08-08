// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/serial.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <limits.h>

__BEGIN_CDECLS;

typedef struct {
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
    uint32_t metadata_count;
    uint32_t reserved[8];
    char name[ZX_MAX_NAME_LEN];
} pdev_device_info_t;

typedef struct {
    // Vendor ID for the board.
    uint32_t vid;
    // Product ID for the board.
    uint32_t pid;
    // Board name from the boot image platform ID record.
    char board_name[ZBI_BOARD_NAME_LEN];
    // Board specific revision number.
    uint32_t board_revision;
} pdev_board_info_t;

typedef struct {
    zx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t (*map_interrupt)(void* ctx, uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t (*get_bti)(void* ctx, uint32_t index, zx_handle_t* out_handle);
    zx_status_t (*get_device_info)(void* ctx, pdev_device_info_t* out_info);
    zx_status_t (*get_board_info)(void* ctx, pdev_board_info_t* out_info);
} platform_device_protocol_ops_t;

typedef struct {
    platform_device_protocol_ops_t* ops;
    void* ctx;
} platform_device_protocol_t;

// Maps an MMIO region. "index" is relative to the list of MMIOs for the device.
static inline zx_status_t pdev_map_mmio(const platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                                        zx_handle_t* out_handle) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, out_vaddr, out_size, NULL,
                               out_handle);
}

static inline zx_status_t pdev_map_mmio2(const platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                                        zx_paddr_t* out_paddr, zx_handle_t* out_handle) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, out_vaddr, out_size, out_paddr,
                               out_handle);
}

// Returns an interrupt handle. "index" is relative to the list of IRQs for the device.
static inline zx_status_t pdev_map_interrupt(const platform_device_protocol_t* pdev, uint32_t index,
                                             zx_handle_t* out_handle) {
    return pdev->ops->map_interrupt(pdev->ctx, index, 0, out_handle);
}

// Returns an interrupt handle. "index" is relative to the list of IRQs for the device.
// This API allows user to specify the mode
static inline zx_status_t pdev_get_interrupt(const platform_device_protocol_t* pdev, uint32_t index,
                                             uint32_t flags, zx_handle_t* out_handle) {
    return pdev->ops->map_interrupt(pdev->ctx, index, flags, out_handle);
}

// Returns an IOMMU bus transaction initiator handle.
// "index" is relative to the list of BTIs for the device.
static inline zx_status_t pdev_get_bti(const platform_device_protocol_t* pdev, uint32_t index,
                                       zx_handle_t* out_handle) {
    return pdev->ops->get_bti(pdev->ctx, index, out_handle);
}

static inline zx_status_t pdev_get_device_info(const platform_device_protocol_t* pdev,
                                               pdev_device_info_t* out_info) {
    return pdev->ops->get_device_info(pdev->ctx, out_info);
}

static inline zx_status_t pdev_get_board_info(const platform_device_protocol_t* pdev,
                                               pdev_board_info_t* out_info) {
    return pdev->ops->get_board_info(pdev->ctx, out_info);
}

// MMIO mapping helpers

static inline zx_status_t pdev_map_mmio_buffer(const platform_device_protocol_t* pdev,
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

__END_CDECLS;
