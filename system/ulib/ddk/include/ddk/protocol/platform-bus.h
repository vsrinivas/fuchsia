// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

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
    mx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** vaddr,
                            size_t* size, mx_handle_t* out_handle);

    mx_status_t (*map_interrupt)(void* ctx, uint32_t index, mx_handle_t* out_handle);
} platform_bus_protocol_ops_t;

typedef struct {
    platform_bus_protocol_ops_t* ops;
    void* ctx;
} platform_bus_protocol_t;

// Registers a protocol with the platform bus driver
static inline mx_status_t pbus_set_interface(platform_bus_protocol_t* pbus,
                                               pbus_interface_t* interface) {
    return pbus->ops->set_interface(pbus->ctx, interface);
}

// Maps an MMIO region based on information in the MDI
// index is based on ordering of the device's mmio nodes in the MDI
static inline mx_status_t pbus_map_mmio(platform_bus_protocol_t* pbus, uint32_t index,
                                        uint32_t cache_policy, void** vaddr, size_t* size,
                                        mx_handle_t* out_handle) {
    return pbus->ops->map_mmio(pbus->ctx, index, cache_policy, vaddr, size, out_handle);
}

// Returns an interrupt handle for an IRQ based on information in the MDI
// index is based on ordering of the device's irq nodes in the MDI
static inline mx_status_t pbus_map_interrupt(platform_bus_protocol_t* pbus, uint32_t index,
                                             mx_handle_t* out_handle) {
    return pbus->ops->map_interrupt(pbus->ctx, index, out_handle);
}

__END_CDECLS;
