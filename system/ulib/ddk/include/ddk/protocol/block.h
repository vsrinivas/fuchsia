// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/device/block.h>

__BEGIN_CDECLS;

typedef struct block_callbacks {
    void (*complete)(void* cookie, mx_status_t status);
} block_callbacks_t;

typedef struct block_protocol_ops {
    void (*set_callbacks)(void* ctx, block_callbacks_t* cb);
    void (*get_info)(void* ctx, block_info_t* info);
    void (*read)(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                 uint64_t dev_offset, void* cookie);
    void (*write)(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                  uint64_t dev_offset, void* cookie);
} block_protocol_ops_t;

typedef struct {
    block_protocol_ops_t* ops;
    void* ctx;
} block_protocol_t;

// Identify how the block device can propagate certain information, such as
// "operation completed".
static inline void block_set_callbacks(block_protocol_t* block, block_callbacks_t* cb) {
    block->ops->set_callbacks(block->ctx, cb);
}

// Get information about the underlying block device
static inline void block_get_info(block_protocol_t* block, block_info_t* info) {
    block->ops->get_info(block->ctx, info);
}

// Read to the VMO from the block device
static inline void block_read(block_protocol_t* block, mx_handle_t vmo, uint64_t length,
                              uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block->ops->read(block->ctx, vmo, length, vmo_offset, dev_offset, cookie);
}


// Write from the VMO to the block device
static inline void block_write(block_protocol_t* block, mx_handle_t vmo, uint64_t length,
                               uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block->ops->write(block->ctx, vmo, length, vmo_offset, dev_offset, cookie);
}

__END_CDECLS;
