// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/listnode.h>

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
    zx_status_t (*get_oob_irq)(void* ctx, zx_handle_t *oob_irq);
    zx_status_t (*enable_fn)(void *ctx, uint8_t fn_idx);
    zx_status_t (*disable_fn)(void *ctx, uint8_t fn_idx);
    zx_status_t (*enable_fn_intr)(void *ctx, uint8_t fn_idx);
    zx_status_t (*disable_fn_intr)(void *ctx, uint8_t fn_idx);
    zx_status_t (*update_block_size)(void *ctx, uint8_t fn_idx, uint16_t blk_sz, bool deflt);
    zx_status_t (*do_rw_txn)(void *ctx, uint8_t fn_idx, sdio_rw_txn_t *txn);
} sdio_protocol_ops_t;

typedef struct sdio_protocol {
    sdio_protocol_ops_t* ops;
    void* ctx;
} sdio_protocol_t;

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

static inline zx_status_t sdio_do_rw_txn(sdio_protocol_t* sdio, uint8_t fn_idx,
                                         sdio_rw_txn_t *txn) {
    return sdio->ops->do_rw_txn(sdio->ctx, fn_idx, txn);
}

static inline zx_status_t sdio_get_oob_irq(sdio_protocol_t* sdio, zx_handle_t *oob_irq) {
    return sdio->ops->get_oob_irq(sdio->ctx, oob_irq);
}
