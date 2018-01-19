// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/block.h>
#include <zircon/listnode.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

namespace zxcrypt {

// |extra_op_t| is the extra information placed in the tail end of |block_op_t|s queued against a
// |zxcrypt::Device|.  The names of the fields are deliberately chosen not to match with those in
// |block_op_t|, since they are in bytes, not blocks.
static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "uintptr_t > uint64_t");
struct extra_op_t {
    union {
        uint8_t* buf;     // Memory region to use for cryptographic transformations.
        extra_op_t* next; // There's no memory available right now, queue this
    };
    uint32_t len;    // Length of request, in BYTES
    uint64_t num;    // Device offset, used as cipher alignment, in BYTES
    uint64_t off;    // VMO offset in BYTES
    zx_handle_t vmo; // VMO of the requester

    void (*completion_cb)(block_op_t* block, zx_status_t status);
    void* cookie;
};

} // namespace zxcrypt
