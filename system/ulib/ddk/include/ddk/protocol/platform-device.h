// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

// DID reserved for the platform bus implementation driver
#define PDEV_BUS_IMPLEMENTOR_DID    0

// interface registered by the platform bus implementation driver
typedef struct {
    mx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, void* out);
    // TODO(voydanoff) Add APIs for GPIOs, clocks I2C, etc
} pbus_interface_ops_t;

typedef struct {
    pbus_interface_ops_t* ops;
    void* ctx;
} pbus_interface_t;

typedef struct {
    mx_status_t (*set_interface)(void* ctx, pbus_interface_t* interface);
    mx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, void* out);
    mx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** vaddr,
                            size_t* size, mx_handle_t* out_handle);

    mx_status_t (*map_interrupt)(void* ctx, uint32_t index, mx_handle_t* out_handle);
} platform_device_protocol_ops_t;

typedef struct {
    platform_device_protocol_ops_t* ops;
    void* ctx;
} platform_device_protocol_t;

static inline mx_status_t pdev_set_interface(platform_device_protocol_t* pdev,
                                             pbus_interface_t* interface) {
    return pdev->ops->set_interface(pdev->ctx, interface);
}

// Requests a given protocol from the platform bus implementation
static inline mx_status_t pdev_get_protocol(platform_device_protocol_t* pdev, uint32_t proto_id,
                                             void* out_proto) {
    return pdev->ops->get_protocol(pdev->ctx, proto_id, out_proto);
}

// Maps an MMIO region based on information in the MDI
// index is based on ordering of the device's mmio nodes in the MDI
static inline mx_status_t pdev_map_mmio(platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, void** vaddr, size_t* size,
                                        mx_handle_t* out_handle) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, vaddr, size, out_handle);
}

// Returns an interrupt handle for an IRQ based on information in the MDI
// index is based on ordering of the device's irq nodes in the MDI
static inline mx_status_t pdev_map_interrupt(platform_device_protocol_t* pdev, uint32_t index,
                                             mx_handle_t* out_handle) {
    return pdev->ops->map_interrupt(pdev->ctx, index, out_handle);
}

__END_CDECLS;
