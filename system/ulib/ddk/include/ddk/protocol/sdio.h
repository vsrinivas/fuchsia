// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <hw/sdio.h>

#define SDIO_FN_0   0
#define SDIO_FN_1   1
#define SDIO_FN_2   2
#define SDIO_MAX_FUNCS 8 // Including func 0

typedef struct sdio_func_hw_info {
    uint32_t manufacturer_id;
    uint32_t product_id;
    uint32_t max_blk_size;
    uint32_t max_tran_speed;
    uint8_t  fn_intf_code;
} sdio_func_hw_info_t;

typedef struct sdio_device_hw_info {
    uint32_t  num_funcs; // number of sdio funcs
    uint32_t  sdio_vsn;
    uint32_t  cccr_vsn;
    uint32_t  caps;
#define SDIO_CARD_MULTI_BLOCK    (1 << 0)
#define SDIO_CARD_SRW            (1 << 1)
#define SDIO_CARD_DIRECT_COMMAND (1 << 2)
#define SDIO_CARD_SUSPEND_RESUME (1 << 3)
#define SDIO_CARD_LOW_SPEED      (1 << 4)
#define SDIO_CARD_HIGH_SPEED     (1 << 5)
#define SDIO_CARD_HIGH_POWER     (1 << 6)
#define SDIO_CARD_4BIT_BUS       (1 << 7)
#define SDIO_CARD_HS_SDR12       (1 << 8)
#define SDIO_CARD_HS_SDR25       (1 << 9)
#define SDIO_CARD_UHS_SDR50      (1 << 10)
#define SDIO_CARD_UHS_SDR104     (1 << 11)
#define SDIO_CARD_UHS_DDR50      (1 << 12)
#define SDIO_DRIVER_TYPE_A       (1 << 13)
#define SDIO_DRIVER_TYPE_B       (1 << 14)
#define SDIO_DRIVER_TYPE_C       (1 << 15)
#define SDIO_DRIVER_TYPE_D       (1 << 16)
} sdio_device_hw_info_t;

typedef struct sdio_hw_info {
    sdio_device_hw_info_t dev_hw_info;
    sdio_func_hw_info_t funcs_hw_info[SDIO_MAX_FUNCS];
    uint32_t host_max_transfer_size;
} sdio_hw_info_t;

typedef struct sdio_rw_txn {
    uint32_t addr;
    uint32_t data_size;
    bool incr;
    bool fifo;
    bool write;
    bool use_dma;
    zx_handle_t dma_vmo; // Used if use_dma is true
    void* virt;          // Used if use_dma is false
    uint64_t buf_offset; // offset into dma_vmo or virt
} sdio_rw_txn_t;

typedef struct sdio_protocol_ops {
    zx_status_t (*get_dev_hw_info)(void* ctx, sdio_hw_info_t *hw_info);
    zx_status_t (*get_oob_irq)(void* ctx, zx_handle_t *oob_irq);
    zx_status_t (*enable_fn)(void *ctx, uint8_t fn_idx);
    zx_status_t (*disable_fn)(void *ctx, uint8_t fn_idx);
    zx_status_t (*enable_fn_intr)(void *ctx, uint8_t fn_idx);
    zx_status_t (*disable_fn_intr)(void *ctx, uint8_t fn_idx);
    zx_status_t (*update_block_size)(void *ctx, uint8_t fn_idx, uint16_t blk_sz, bool deflt);
    zx_status_t (*get_block_size)(void *ctx, uint8_t fn_idx, uint16_t *cur_blk_size);
    zx_status_t (*do_rw_txn)(void *ctx, uint8_t fn_idx, sdio_rw_txn_t *txn);
} sdio_protocol_ops_t;

typedef struct sdio_protocol {
    sdio_protocol_ops_t* ops;
    void* ctx;
} sdio_protocol_t;

static inline bool sdio_fn_idx_valid(uint8_t fn_idx) {
    return (fn_idx < SDIO_MAX_FUNCS);
}

static inline zx_status_t sdio_enable_fn(sdio_protocol_t* sdio, uint8_t fn_idx) {
    return sdio->ops->enable_fn(sdio->ctx, fn_idx);
}

static inline zx_status_t sdio_disable_fn(sdio_protocol_t* sdio, uint8_t fn_idx) {
    return sdio->ops->disable_fn(sdio->ctx, fn_idx);
}

static inline zx_status_t sdio_enable_fn_intr(sdio_protocol_t* sdio, uint8_t fn_idx) {
    return sdio->ops->enable_fn_intr(sdio->ctx, fn_idx);
}

static inline zx_status_t sdio_disable_fn_intr(sdio_protocol_t* sdio, uint8_t fn_idx) {
    return sdio->ops->disable_fn_intr(sdio->ctx, fn_idx);
}

static inline zx_status_t sdio_update_block_size(sdio_protocol_t* sdio, uint8_t fn_idx,
                                                 uint16_t blk_sz, bool deflt) {
    return sdio->ops->update_block_size(sdio->ctx, fn_idx, blk_sz, deflt);
}

static inline zx_status_t sdio_get_block_size(sdio_protocol_t* sdio, uint8_t fn_idx,
                                              uint16_t *cur_blk_size) {
    return sdio->ops->get_block_size(sdio->ctx, fn_idx, cur_blk_size);
}

static inline zx_status_t sdio_do_rw_txn(sdio_protocol_t* sdio, uint8_t fn_idx,
                                         sdio_rw_txn_t *txn) {
    return sdio->ops->do_rw_txn(sdio->ctx, fn_idx, txn);
}

static inline zx_status_t sdio_get_oob_irq(sdio_protocol_t* sdio, zx_handle_t *oob_irq) {
    return sdio->ops->get_oob_irq(sdio->ctx, oob_irq);
}

static inline zx_status_t sdio_get_dev_hw_info(sdio_protocol_t* sdio,
                                            sdio_hw_info_t *dev_info) {
    return sdio->ops->get_dev_hw_info(sdio->ctx, dev_info);
}
