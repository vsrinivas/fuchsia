// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/device/block.h>

typedef struct block_callbacks {
    void (*complete)(void* cookie, mx_status_t status);
} block_callbacks_t;

typedef struct block_protocol_ops {
    // Identify how the block device can propagate certain information, such as
    // "operation completed".
    void (*set_callbacks)(void* ctx, block_callbacks_t* cb);

    // Get information about the underlying block device
    void (*get_info)(void* ctx, block_info_t* info);

    // Read to the VMO from the block device
    void (*read)(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                 uint64_t dev_offset, void* cookie);

    // Write from the VMO to the block device
    void (*write)(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                  uint64_t dev_offset, void* cookie);
} block_protocol_ops_t;

typedef struct {
    block_protocol_ops_t* ops;
    void* ctx;
} block_protocol_t;
