// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bad_block.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct bad_block_protocol bad_block_protocol_t;

// Declarations

typedef struct bad_block_protocol_ops {
    zx_status_t (*get_bad_block_list)(void* ctx, uint32_t* out_bad_blocks_list,
                                      size_t bad_blocks_count, size_t* out_bad_blocks_actual);
    zx_status_t (*mark_block_bad)(void* ctx, uint32_t block);
} bad_block_protocol_ops_t;

struct bad_block_protocol {
    bad_block_protocol_ops_t* ops;
    void* ctx;
};

// Fills in |bad_blocks| with a list of bad blocks, up until
// |bad_blocks_count|. The order of blocks is undefined.
// |bad_blocks_actual| will be filled in with the actual number of bad
// blocks. It is recommended to first make call with |bad_blocks_count|
// equal to 0 in order to determine how large the |bad_blocks| is.
static inline zx_status_t bad_block_get_bad_block_list(const bad_block_protocol_t* proto,
                                                       uint32_t* out_bad_blocks_list,
                                                       size_t bad_blocks_count,
                                                       size_t* out_bad_blocks_actual) {
    return proto->ops->get_bad_block_list(proto->ctx, out_bad_blocks_list, bad_blocks_count,
                                          out_bad_blocks_actual);
}
// Sets |block| as bad. If block is already marked bad, it has no effect.
static inline zx_status_t bad_block_mark_block_bad(const bad_block_protocol_t* proto,
                                                   uint32_t block) {
    return proto->ops->mark_block_bad(proto->ctx, block);
}

__END_CDECLS;
