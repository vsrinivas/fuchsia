// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    // physical address of MMIO region
    // does not need to be page aligned
    zx_paddr_t  base;
    // length of MMIO region in bytes
    // does not need to be page aligned
    size_t      length;
} pbus_mmio_t;

typedef struct {
    uint32_t    irq;
    uint32_t    mode;   // ZX_INTERRUPT_MODE_* flags
} pbus_irq_t;

typedef struct {
    uint32_t    gpio;
} pbus_gpio_t;

typedef struct {
    uint32_t    bus_id;
    uint16_t    address;
} pbus_i2c_channel_t;

typedef struct {
    uint32_t clk;
} pbus_clk_t;

typedef struct {
    uint32_t    iommu_index;
    uint32_t    bti_id;
} pbus_bti_t;

// metadata for the device
typedef struct {
    uint32_t    type;   // metadata type (matches zbi_header_t.type for bootloader metadata)
    uint32_t    extra;  // matches zbi_header_t.extra for bootloader metadata
    const void* data;   // pointer to metadata (set to NULL for bootloader metadata)
    uint32_t    len;    // metadata length in bytes (set to zero for bootloader metadata)
} pbus_metadata_t;

typedef struct pbus_dev pbus_dev_t;
struct pbus_dev {
    const char* name;
    uint32_t vid;   // BIND_PLATFORM_DEV_VID
    uint32_t pid;   // BIND_PLATFORM_DEV_PID
    uint32_t did;   // BIND_PLATFORM_DEV_DID
    const pbus_mmio_t* mmios;
    uint32_t mmio_count;
    const pbus_irq_t* irqs;
    uint32_t irq_count;
    const pbus_gpio_t* gpios;
    uint32_t gpio_count;
    const pbus_i2c_channel_t* i2c_channels;
    uint32_t i2c_channel_count;
    const pbus_clk_t* clks;
    uint32_t clk_count;
    const pbus_bti_t* btis;
    uint32_t bti_count;
    const pbus_metadata_t* metadata;
    uint32_t metadata_count;
    // List of this device's child devices.
    // This is only used in cases where children of a platform device also need to access
    // platform bus resources.
    const pbus_dev_t* children;
    uint32_t child_count;
};

// Subset of pdev_board_info_t to be set by the board driver.
typedef struct {
    // Board specific revision number.
    uint32_t board_revision;
} pbus_board_info_t;

typedef struct {
    zx_status_t (*device_add)(void* ctx, const pbus_dev_t* dev);
    zx_status_t (*protocol_device_add)(void* ctx, uint32_t proto_id, const pbus_dev_t* dev);
    zx_status_t (*register_protocol)(void* ctx, uint32_t proto_id, void* protocol);
    const char* (*get_board_name)(void* ctx);
    zx_status_t (*set_board_info)(void* ctx, const pbus_board_info_t* info);
} platform_bus_protocol_ops_t;

typedef struct {
    platform_bus_protocol_ops_t* ops;
    void* ctx;
} platform_bus_protocol_t;

// Adds a new platform device to the bus, using configuration provided by "dev".
// Platform devices are created in their own separate devhosts.
static inline zx_status_t pbus_device_add(const platform_bus_protocol_t* pbus,
                                          const pbus_dev_t* dev) {
    return pbus->ops->device_add(pbus->ctx, dev);
}

// Adds a device for binding a protocol implementation driver.
// These devices are added in the same devhost as the platform bus.
// After the driver binds to the device it calls pbus_register_protocol()
// to register its protocol with the platform bus.
// pbus_protocol_device_add() blocks until the protocol implementation driver
// registers its protocol (or times out).
static inline zx_status_t pbus_protocol_device_add(const platform_bus_protocol_t* pbus,
                                                   uint32_t proto_id, const pbus_dev_t* dev) {
    return pbus->ops->protocol_device_add(pbus->ctx, proto_id, dev);
}

// Called by protocol implementation drivers to register their protocol
// with the platform bus.
static inline zx_status_t pbus_register_protocol(const platform_bus_protocol_t* pbus,
                                                 uint32_t proto_id, void* protocol) {
    return pbus->ops->register_protocol(pbus->ctx, proto_id, protocol);
}

// Returns the board name for the underlying hardware.
// Board drivers may use this to differentiate between multiple boards that they support.
static inline const char* pbus_get_board_name(const platform_bus_protocol_t* pbus) {
    return pbus->ops->get_board_name(pbus->ctx);
}

// Board drivers may use this to set information about the board
// (like the board revision number).
// Platform device drivers can access this via pdev_get_board_info().
static inline zx_status_t pbus_set_board_info(const platform_bus_protocol_t* pbus,
                                              const pbus_board_info_t* info) {
    return pbus->ops->set_board_info(pbus->ctx, info);
}

__END_CDECLS;
