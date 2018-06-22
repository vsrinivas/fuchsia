// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <ddk/protocol/block.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>

#include "extra.h"

namespace zxcrypt {

void extra_op_t::Init() {
    list_initialize(&node);
    data = nullptr;
    length = 0;
    offset_dev = 0;
    offset_vmo = 0;
    completion_cb = nullptr;
    cookie = nullptr;
}

extra_op_t* BlockToExtra(block_op_t* block, size_t op_size) {
    ZX_DEBUG_ASSERT(block);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(block);
    return reinterpret_cast<extra_op_t*>(ptr + op_size) - 1;
}

block_op_t* ExtraToBlock(extra_op_t* extra, size_t op_size) {
    ZX_DEBUG_ASSERT(extra);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(extra + 1);
    return reinterpret_cast<block_op_t*>(ptr - op_size);
}

} // namespace zxcrypt
