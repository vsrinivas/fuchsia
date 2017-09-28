// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    zx_paddr_t  base;
    size_t      length;
    uint32_t    id;
} pbus_mmio_t;

typedef struct {
    uint32_t    irq;
    uint32_t    id;
} pbus_irq_t;

typedef struct {
    const char* name;
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    const pbus_mmio_t* mmios;
    size_t mmio_count;
    const pbus_irq_t* irqs;
    size_t irq_count;
} pbus_dev_t;

// flags for pbus_device_add()
enum {
    // Add the device but to not publish it to the devmgr until enabled with pbus_device_enable().
    PDEV_ADD_DISABLED = (1 << 0),
};

// DID reserved for the platform bus implementation driver
#define PDEV_BUS_IMPLEMENTOR_DID    0

// interface registered by the platform bus implementation driver
typedef struct {
    zx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, void* out);
} pbus_interface_ops_t;

typedef struct {
    pbus_interface_ops_t* ops;
    void* ctx;
} pbus_interface_t;

static inline zx_status_t pbus_interface_get_protocol(pbus_interface_t* intf, uint32_t proto_id,
                                                      void* out) {
    return intf->ops->get_protocol(intf->ctx, proto_id, out);
}

typedef struct {
    zx_status_t (*set_interface)(void* ctx, pbus_interface_t* interface);
    zx_status_t (*device_add)(void* ctx, const pbus_dev_t* dev, uint32_t flags);
    zx_status_t (*device_enable)(void* ctx, uint32_t vid, uint32_t pid, uint32_t did, bool enable);
    const char* (*get_board_name)(void* ctx);
} platform_bus_protocol_ops_t;

typedef struct {
    platform_bus_protocol_ops_t* ops;
    void* ctx;
} platform_bus_protocol_t;

static inline zx_status_t pbus_set_interface(platform_bus_protocol_t* pbus,
                                             pbus_interface_t* interface) {
    return pbus->ops->set_interface(pbus->ctx, interface);
}

static inline zx_status_t pbus_device_add(platform_bus_protocol_t* pbus, const pbus_dev_t* dev,
                                          uint32_t flags) {
    return pbus->ops->device_add(pbus->ctx, dev, flags);
}

// Dynamically enables or disables a platform device by adding or removing it
// from the DDK device tree.
static inline zx_status_t pbus_device_enable(platform_bus_protocol_t* pbus, uint32_t vid,
                                             uint32_t pid, uint32_t did, bool enable) {
    return pbus->ops->device_enable(pbus->ctx, vid, pid, did, enable);
}

static inline const char* pbus_get_board_name(platform_bus_protocol_t* pbus) {
    return pbus->ops->get_board_name(pbus->ctx);
}

__END_CDECLS;
