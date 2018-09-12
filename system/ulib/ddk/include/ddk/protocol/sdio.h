// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdio.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct sdio_device_hw_info sdio_device_hw_info_t;
typedef struct sdio_func_hw_info sdio_func_hw_info_t;
typedef struct sdio_hw_info sdio_hw_info_t;
typedef struct sdio_rw_txn sdio_rw_txn_t;
typedef struct sdio_protocol sdio_protocol_t;
typedef uint32_t sdio_card_t;
#define SDIO_CARD_MULTI_BLOCK UINT32_C(1)
#define SDIO_CARD_SRW UINT32_C(2)
#define SDIO_CARD_DIRECT_COMMAND UINT32_C(4)
#define SDIO_CARD_SUSPEND_RESUME UINT32_C(8)
#define SDIO_CARD_LOW_SPEED UINT32_C(16)
#define SDIO_CARD_HIGH_SPEED UINT32_C(32)
#define SDIO_CARD_HIGH_POWER UINT32_C(64)
#define SDIO_CARD_FOUR_BIT_BUS UINT32_C(128)
#define SDIO_CARD_HS_SDR12 UINT32_C(256)
#define SDIO_CARD_HS_SDR25 UINT32_C(512)
#define SDIO_CARD_UHS_SDR50 UINT32_C(1024)
#define SDIO_CARD_UHS_SDR104 UINT32_C(2048)
#define SDIO_CARD_UHS_DDR50 UINT32_C(4096)
#define SDIO_CARD_TYPE_A UINT32_C(8192)
#define SDIO_CARD_TYPE_B UINT32_C(16384)
#define SDIO_CARD_TYPE_C UINT32_C(32768)
#define SDIO_CARD_TYPE_D UINT32_C(65536)

// Declarations

struct sdio_device_hw_info {
    // number of sdio funcs
    uint32_t num_funcs;
    uint32_t sdio_vsn;
    uint32_t cccr_vsn;
    uint32_t caps;
};

struct sdio_func_hw_info {
    uint32_t manufacturer_id;
    uint32_t product_id;
    uint32_t max_blk_size;
    uint32_t max_tran_speed;
    uint8_t fn_intf_code;
};

struct sdio_hw_info {
    sdio_device_hw_info_t dev_hw_info;
    sdio_func_hw_info_t funcs_hw_info[8];
    uint32_t host_max_transfer_size;
};

// Including func 0
#define SDIO_MAX_FUNCS UINT8_C(8)

struct sdio_rw_txn {
    uint32_t addr;
    uint32_t data_size;
    bool incr;
    bool fifo;
    bool write;
    bool use_dma;
    // Used if use_dma is true
    zx_handle_t dma_vmo;
    // Used if use_dma is false
    void* virt_buffer;
    size_t virt_size;
    // offset into dma_vmo or virt
    uint64_t buf_offset;
};

typedef struct sdio_protocol_ops {
    zx_status_t (*get_dev_hw_info)(void* ctx, sdio_hw_info_t* out_hw_info);
    zx_status_t (*enable_fn)(void* ctx, uint8_t fn_idx);
    zx_status_t (*disable_fn)(void* ctx, uint8_t fn_idx);
    zx_status_t (*enable_fn_intr)(void* ctx, uint8_t fn_idx);
    zx_status_t (*disable_fn_intr)(void* ctx, uint8_t fn_idx);
    zx_status_t (*update_block_size)(void* ctx, uint8_t fn_idx, uint16_t blk_sz, bool deflt);
    zx_status_t (*get_block_size)(void* ctx, uint8_t fn_idx, uint16_t* out_cur_blk_size);
    zx_status_t (*do_rw_txn)(void* ctx, uint8_t fn_idx, sdio_rw_txn_t* txn);
    zx_status_t (*do_rw_byte)(void* ctx, bool write, uint8_t fn_idx, uint32_t addr,
                              uint8_t write_byte, uint8_t* out_read_byte);
} sdio_protocol_ops_t;

struct sdio_protocol {
    sdio_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t sdio_get_dev_hw_info(const sdio_protocol_t* proto,
                                               sdio_hw_info_t* out_hw_info) {
    return proto->ops->get_dev_hw_info(proto->ctx, out_hw_info);
}
static inline zx_status_t sdio_enable_fn(const sdio_protocol_t* proto, uint8_t fn_idx) {
    return proto->ops->enable_fn(proto->ctx, fn_idx);
}
static inline zx_status_t sdio_disable_fn(const sdio_protocol_t* proto, uint8_t fn_idx) {
    return proto->ops->disable_fn(proto->ctx, fn_idx);
}
static inline zx_status_t sdio_enable_fn_intr(const sdio_protocol_t* proto, uint8_t fn_idx) {
    return proto->ops->enable_fn_intr(proto->ctx, fn_idx);
}
static inline zx_status_t sdio_disable_fn_intr(const sdio_protocol_t* proto, uint8_t fn_idx) {
    return proto->ops->disable_fn_intr(proto->ctx, fn_idx);
}
static inline zx_status_t sdio_update_block_size(const sdio_protocol_t* proto, uint8_t fn_idx,
                                                 uint16_t blk_sz, bool deflt) {
    return proto->ops->update_block_size(proto->ctx, fn_idx, blk_sz, deflt);
}
static inline zx_status_t sdio_get_block_size(const sdio_protocol_t* proto, uint8_t fn_idx,
                                              uint16_t* out_cur_blk_size) {
    return proto->ops->get_block_size(proto->ctx, fn_idx, out_cur_blk_size);
}
static inline zx_status_t sdio_do_rw_txn(const sdio_protocol_t* proto, uint8_t fn_idx,
                                         sdio_rw_txn_t* txn) {
    return proto->ops->do_rw_txn(proto->ctx, fn_idx, txn);
}
static inline zx_status_t sdio_do_rw_byte(const sdio_protocol_t* proto, bool write, uint8_t fn_idx,
                                          uint32_t addr, uint8_t write_byte,
                                          uint8_t* out_read_byte) {
    return proto->ops->do_rw_byte(proto->ctx, write, fn_idx, addr, write_byte, out_read_byte);
}

#define SDIO_FN_2 UINT8_C(2)

#define SDIO_FN_1 UINT8_C(1)

#define SDIO_FN_0 UINT8_C(0)

__END_CDECLS;
