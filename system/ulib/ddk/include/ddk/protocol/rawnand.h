// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <ddk/protocol/nand.h>

__BEGIN_CDECLS;

typedef struct raw_nand_protocol_ops {
    // Read one nand page with hwecc.
    zx_status_t (*read_page_hwecc)(void* ctx, void *data, void *oob,
                                   uint32_t nandpage, int *ecc_correct);
    // Write one nand page with hwecc.
    zx_status_t (*write_page_hwecc)(void* ctx, const void* data, const void* oob,
                                    uint32_t nandpage);
    // Erase nand block.
    zx_status_t (*erase_block)(void* ctx, uint32_t nandpage);
    zx_status_t (*get_nand_info)(void *ctx, struct nand_info *info);
    // Send ONFI command down to controller.
    void (*cmd_ctrl)(void *ctx, int32_t cmd, uint32_t ctrl);
    // Read byte (used to read status as well as other info, such as ID).
    uint8_t (*read_byte)(void *ctx);
} raw_nand_protocol_ops_t;

typedef struct raw_nand_protocol {
    raw_nand_protocol_ops_t *ops;
    void *ctx;
} raw_nand_protocol_t;

static inline zx_status_t raw_nand_read_page_hwecc(raw_nand_protocol_t *raw_nand,
                                                   void *data, void *oob,
                                                   uint32_t nand_page,
                                                   int *ecc_correct)
{
    return raw_nand->ops->read_page_hwecc(raw_nand->ctx, data, oob, nand_page,
                                         ecc_correct);
}

static inline zx_status_t raw_nand_write_page_hwecc(raw_nand_protocol_t* raw_nand,
                                                    const void* data, const void* oob,
                                                    uint32_t nand_page)
{
    return raw_nand->ops->write_page_hwecc(raw_nand->ctx, data, oob, nand_page);
}

static inline zx_status_t raw_nand_erase_block(raw_nand_protocol_t *raw_nand,
                                               uint32_t nand_page)
{
    return raw_nand->ops->erase_block(raw_nand->ctx, nand_page);
}

static inline zx_status_t raw_nand_get_info(raw_nand_protocol_t *raw_nand,
                                            struct nand_info *info)
{
    return raw_nand->ops->get_nand_info(raw_nand->ctx, info);
}

static inline void raw_nand_cmd_ctrl(raw_nand_protocol_t *raw_nand,
                                     int32_t cmd, uint32_t ctrl)
{
    raw_nand->ops->cmd_ctrl(raw_nand->ctx, cmd, ctrl);
}

static inline uint8_t raw_nand_read_byte(raw_nand_protocol_t *raw_nand)
{
    return raw_nand->ops->read_byte(raw_nand->ctx);
}

__END_CDECLS;

