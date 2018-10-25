// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_device.banjo INSTEAD.

#pragma once

#include <ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct pdev_device_info pdev_device_info_t;
typedef struct pdev_board_info pdev_board_info_t;
typedef struct pdev_mmio pdev_mmio_t;
typedef struct pdev_protocol pdev_protocol_t;

// Declarations

struct pdev_device_info {
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint32_t gpio_count;
    uint32_t i2c_channel_count;
    uint32_t clk_count;
    uint32_t bti_count;
    uint32_t smc_count;
    uint32_t metadata_count;
    uint32_t reserved[8];
    char name[32];
};

struct pdev_board_info {
    // Vendor ID for the board.
    uint32_t vid;
    // Product ID for the board.
    uint32_t pid;
    // Board name from the boot image platform ID record.
    char board_name[32];
    // Board specific revision number.
    uint32_t board_revision;
};

struct pdev_mmio {
    // Offset from beginning of VMO where the mmio region begins.
    uint64_t offset;
    // Size of mmio region.
    size_t size;
    zx_handle_t vmo;
};

typedef struct pdev_protocol_ops {
    zx_status_t (*get_mmio)(void* ctx, uint32_t index, pdev_mmio_t* out_mmio);
    zx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy,
                            void** out_vaddr_buffer, size_t* vaddr_size, uint64_t* out_paddr,
                            zx_handle_t* out_handle);
    zx_status_t (*get_interrupt)(void* ctx, uint32_t index, uint32_t flags, zx_handle_t* out_irq);
    zx_status_t (*get_bti)(void* ctx, uint32_t index, zx_handle_t* out_bti);
    zx_status_t (*get_smc)(void* ctx, uint32_t index, zx_handle_t* out_smc);
    zx_status_t (*get_device_info)(void* ctx, pdev_device_info_t* out_info);
    zx_status_t (*get_board_info)(void* ctx, pdev_board_info_t* out_info);
    zx_status_t (*device_add)(void* ctx, uint32_t index, const device_add_args_t* args,
                              zx_device_t** out_device);
    zx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, uint32_t index,
                                void* out_out_protocol_buffer, size_t out_protocol_size,
                                size_t* out_out_protocol_actual);
} pdev_protocol_ops_t;

struct pdev_protocol {
    pdev_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t pdev_get_mmio(const pdev_protocol_t* proto, uint32_t index,
                                        pdev_mmio_t* out_mmio) {
    return proto->ops->get_mmio(proto->ctx, index, out_mmio);
}
static inline zx_status_t pdev_map_mmio(const pdev_protocol_t* proto, uint32_t index,
                                        uint32_t cache_policy, void** out_vaddr_buffer,
                                        size_t* vaddr_size, uint64_t* out_paddr,
                                        zx_handle_t* out_handle) {
    return proto->ops->map_mmio(proto->ctx, index, cache_policy, out_vaddr_buffer, vaddr_size,
                                out_paddr, out_handle);
}
static inline zx_status_t pdev_get_interrupt(const pdev_protocol_t* proto, uint32_t index,
                                             uint32_t flags, zx_handle_t* out_irq) {
    return proto->ops->get_interrupt(proto->ctx, index, flags, out_irq);
}
static inline zx_status_t pdev_get_bti(const pdev_protocol_t* proto, uint32_t index,
                                       zx_handle_t* out_bti) {
    return proto->ops->get_bti(proto->ctx, index, out_bti);
}
static inline zx_status_t pdev_get_smc(const pdev_protocol_t* proto, uint32_t index,
                                       zx_handle_t* out_smc) {
    return proto->ops->get_smc(proto->ctx, index, out_smc);
}
static inline zx_status_t pdev_get_device_info(const pdev_protocol_t* proto,
                                               pdev_device_info_t* out_info) {
    return proto->ops->get_device_info(proto->ctx, out_info);
}
static inline zx_status_t pdev_get_board_info(const pdev_protocol_t* proto,
                                              pdev_board_info_t* out_info) {
    return proto->ops->get_board_info(proto->ctx, out_info);
}
static inline zx_status_t pdev_device_add(const pdev_protocol_t* proto, uint32_t index,
                                          const device_add_args_t* args, zx_device_t** out_device) {
    return proto->ops->device_add(proto->ctx, index, args, out_device);
}
static inline zx_status_t pdev_get_protocol(const pdev_protocol_t* proto, uint32_t proto_id,
                                            uint32_t index, void* out_out_protocol_buffer,
                                            size_t out_protocol_size,
                                            size_t* out_out_protocol_actual) {
    return proto->ops->get_protocol(proto->ctx, proto_id, index, out_out_protocol_buffer,
                                    out_protocol_size, out_out_protocol_actual);
}

__END_CDECLS;
