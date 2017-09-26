// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// DID reserved for the platform bus implementation driver
#define PDEV_BUS_IMPLEMENTOR_DID    0

// interface registered by the platform bus implementation driver
typedef struct {
    zx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, void* out);
    zx_status_t (*add_gpios)(void* ctx, uint32_t start, uint32_t count, uint32_t mmio_index,
                             const uint32_t* irqs, uint32_t irq_count);

    // TODO(voydanoff) Add APIs for GPIOs, clocks I2C, etc
} pbus_interface_ops_t;

typedef struct {
    pbus_interface_ops_t* ops;
    void* ctx;
} pbus_interface_t;

static inline zx_status_t pbus_interface_get_protocol(pbus_interface_t* intf, uint32_t proto_id,
                                                      void* out) {
    return intf->ops->get_protocol(intf->ctx, proto_id, out);
}

static inline zx_status_t pbus_interface_add_gpios(pbus_interface_t* intf, uint32_t start,
                                                   uint32_t count, uint32_t mmio_index,
                                                   const uint32_t* irqs, uint32_t irq_count) {
    return intf->ops->add_gpios(intf->ctx, start, count, mmio_index, irqs, irq_count);
}

typedef struct {
    zx_status_t (*set_interface)(void* ctx, pbus_interface_t* interface);
    zx_status_t (*device_enable)(void* ctx, uint32_t vid, uint32_t pid, uint32_t did, bool enable);
} platform_bus_protocol_ops_t;

typedef struct {
    platform_bus_protocol_ops_t* ops;
    void* ctx;
} platform_bus_protocol_t;

static inline zx_status_t pbus_set_interface(platform_bus_protocol_t* pbus,
                                             pbus_interface_t* interface) {
    return pbus->ops->set_interface(pbus->ctx, interface);
}

// Dynamically enables or disables a platform device by adding or removing it
// from the DDK device tree.
static inline zx_status_t pbus_device_enable(platform_bus_protocol_t* pbus, uint32_t vid,
                                             uint32_t pid, uint32_t did, bool enable) {
    return pbus->ops->device_enable(pbus->ctx, vid, pid, did, enable);
}

__END_CDECLS;
