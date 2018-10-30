// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/rawnand.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/device/nand.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct raw_nand_protocol raw_nand_protocol_t;

// Declarations

typedef struct raw_nand_protocol_ops {
    zx_status_t (*read_page_hwecc)(void* ctx, uint32_t nandpage, void* out_data_buffer,
                                   size_t data_size, size_t* out_data_actual, void* out_oob_buffer,
                                   size_t oob_size, size_t* out_oob_actual,
                                   uint32_t* out_ecc_correct);
    zx_status_t (*write_page_hwecc)(void* ctx, const void* data_buffer, size_t data_size,
                                    const void* oob_buffer, size_t oob_size, uint32_t nandpage);
    zx_status_t (*erase_block)(void* ctx, uint32_t nandpage);
    zx_status_t (*get_nand_info)(void* ctx, nand_info_t* out_info);
} raw_nand_protocol_ops_t;

struct raw_nand_protocol {
    raw_nand_protocol_ops_t* ops;
    void* ctx;
};

// Read one nand page with hwecc.
static inline zx_status_t
raw_nand_read_page_hwecc(const raw_nand_protocol_t* proto, uint32_t nandpage, void* out_data_buffer,
                         size_t data_size, size_t* out_data_actual, void* out_oob_buffer,
                         size_t oob_size, size_t* out_oob_actual, uint32_t* out_ecc_correct) {
    return proto->ops->read_page_hwecc(proto->ctx, nandpage, out_data_buffer, data_size,
                                       out_data_actual, out_oob_buffer, oob_size, out_oob_actual,
                                       out_ecc_correct);
}
// Write one nand page with hwecc.
static inline zx_status_t raw_nand_write_page_hwecc(const raw_nand_protocol_t* proto,
                                                    const void* data_buffer, size_t data_size,
                                                    const void* oob_buffer, size_t oob_size,
                                                    uint32_t nandpage) {
    return proto->ops->write_page_hwecc(proto->ctx, data_buffer, data_size, oob_buffer, oob_size,
                                        nandpage);
}
// Erase nand block.
static inline zx_status_t raw_nand_erase_block(const raw_nand_protocol_t* proto,
                                               uint32_t nandpage) {
    return proto->ops->erase_block(proto->ctx, nandpage);
}
static inline zx_status_t raw_nand_get_nand_info(const raw_nand_protocol_t* proto,
                                                 nand_info_t* out_info) {
    return proto->ops->get_nand_info(proto->ctx, out_info);
}

__END_CDECLS;
