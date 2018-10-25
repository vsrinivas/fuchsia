// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_bus.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct pbus_boot_metadata pbus_boot_metadata_t;
typedef struct platform_proxy_cb platform_proxy_cb_t;
typedef struct pbus_i2c_channel pbus_i2c_channel_t;
typedef struct pbus_board_info pbus_board_info_t;
typedef struct pbus_metadata pbus_metadata_t;
typedef struct pbus_mmio pbus_mmio_t;
typedef struct pbus_gpio pbus_gpio_t;
typedef struct pbus_smc pbus_smc_t;
typedef struct pbus_irq pbus_irq_t;
typedef struct pbus_dev pbus_dev_t;
typedef struct pbus_protocol pbus_protocol_t;
typedef struct pbus_clk pbus_clk_t;
typedef struct pbus_bti pbus_bti_t;

// Declarations

// Device metadata to be passed from bootloader via a ZBI record.
struct pbus_boot_metadata {
    // Metadata type (matches `zbi_header_t.type` for bootloader metadata).
    uint32_t zbi_type;
    // Matches `zbi_header_t.extra` for bootloader metadata.
    // Used in cases where bootloader provides multiple metadata records of the same type.
    uint32_t zbi_extra;
};

struct platform_proxy_cb {
    void (*callback)(void* ctx, const void* req_buffer, size_t req_size,
                     const zx_handle_t* req_handle_list, size_t req_handle_count,
                     void* out_resp_buffer, size_t resp_size, size_t* out_resp_actual,
                     zx_handle_t* out_resp_handle_list, size_t resp_handle_count,
                     size_t* out_resp_handle_actual);
    void* ctx;
};

struct pbus_i2c_channel {
    uint32_t bus_id;
    uint16_t address;
};

// Subset of pdev_board_info_t to be set by the board driver.
struct pbus_board_info {
    // Board specific revision number.
    uint32_t board_revision;
};

// Device metadata.
struct pbus_metadata {
    // Metadata type.
    uint32_t type;
    // Pointer to metadata.
    const void* data_buffer;
    size_t data_size;
};

struct pbus_mmio {
    // Physical address of MMIO region.
    // Does not need to be page aligned.
    uint64_t base;
    // Length of MMIO region in bytes.
    // Does not need to be page aligned.
    size_t length;
};

struct pbus_gpio {
    uint32_t gpio;
};

struct pbus_smc {
    // The device is granted the ability to make SMC calls with service call numbers ranging from
    // service_call_num_base to service_call_num_base + count - 1.
    uint32_t service_call_num_base;
    uint32_t count;
};

struct pbus_irq {
    uint32_t irq;
    // `ZX_INTERRUPT_MODE_*` flags
    uint32_t mode;
};

struct pbus_dev {
    const char* name;
    // `BIND_PLATFORM_DEV_VID`
    uint32_t vid;
    // `BIND_PLATFORM_DEV_PID`
    uint32_t pid;
    // `BIND_PLATFORM_DEV_DID`
    uint32_t did;
    const pbus_mmio_t* mmio_list;
    size_t mmio_count;
    const pbus_irq_t* irq_list;
    size_t irq_count;
    const pbus_gpio_t* gpio_list;
    size_t gpio_count;
    const pbus_i2c_channel_t* i2c_channel_list;
    size_t i2c_channel_count;
    const pbus_clk_t* clk_list;
    size_t clk_count;
    const pbus_bti_t* bti_list;
    size_t bti_count;
    const pbus_smc_t* smc_list;
    size_t smc_count;
    const pbus_metadata_t* metadata_list;
    size_t metadata_count;
    const pbus_boot_metadata_t* boot_metadata_list;
    size_t boot_metadata_count;
    // List of this device's child devices.
    // This is only used in cases where children of a platform device also need to access
    // platform bus resources.
    const pbus_dev_t* child_list;
    size_t child_count;
    // Extra protocols to be provided to this platform device and its children.
    // These fields are only used for the top level `pbus_dev_t`.
    const uint32_t* protocol_list;
    size_t protocol_count;
};

typedef struct pbus_protocol_ops {
    zx_status_t (*device_add)(void* ctx, const pbus_dev_t* dev);
    zx_status_t (*protocol_device_add)(void* ctx, uint32_t proto_id, const pbus_dev_t* dev);
    zx_status_t (*register_protocol)(void* ctx, uint32_t proto_id, const void* protocol_buffer,
                                     size_t protocol_size, const platform_proxy_cb_t* proxy_cb);
    const char* (*get_board_name)(void* ctx);
    zx_status_t (*set_board_info)(void* ctx, const pbus_board_info_t* info);
} pbus_protocol_ops_t;

struct pbus_protocol {
    pbus_protocol_ops_t* ops;
    void* ctx;
};

// Adds a new platform device to the bus, using configuration provided by |dev|.
// Platform devices are created in their own separate devhosts.
static inline zx_status_t pbus_device_add(const pbus_protocol_t* proto, const pbus_dev_t* dev) {
    return proto->ops->device_add(proto->ctx, dev);
}
// Adds a device for binding a protocol implementation driver.
// These devices are added in the same devhost as the platform bus.
// After the driver binds to the device it calls `pbus_register_protocol()`
// to register its protocol with the platform bus.
// `pbus_protocol_device_add()` blocks until the protocol implementation driver
// registers its protocol (or times out).
static inline zx_status_t pbus_protocol_device_add(const pbus_protocol_t* proto, uint32_t proto_id,
                                                   const pbus_dev_t* dev) {
    return proto->ops->protocol_device_add(proto->ctx, proto_id, dev);
}
// Called by protocol implementation drivers to register their protocol
// with the platform bus.
static inline zx_status_t pbus_register_protocol(const pbus_protocol_t* proto, uint32_t proto_id,
                                                 const void* protocol_buffer, size_t protocol_size,
                                                 const platform_proxy_cb_t* proxy_cb) {
    return proto->ops->register_protocol(proto->ctx, proto_id, protocol_buffer, protocol_size,
                                         proxy_cb);
}
// Returns the board name for the underlying hardware.
// Board drivers may use this to differentiate between multiple boards that they support.
static inline const char* pbus_get_board_name(const pbus_protocol_t* proto) {
    return proto->ops->get_board_name(proto->ctx);
}
// Board drivers may use this to set information about the board
// (like the board revision number).
// Platform device drivers can access this via `pdev_get_board_info()`.
static inline zx_status_t pbus_set_board_info(const pbus_protocol_t* proto,
                                              const pbus_board_info_t* info) {
    return proto->ops->set_board_info(proto->ctx, info);
}

struct pbus_clk {
    uint32_t clk;
};

struct pbus_bti {
    uint32_t iommu_index;
    uint32_t bti_id;
};

__END_CDECLS;
