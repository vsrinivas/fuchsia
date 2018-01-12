// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <zircon/types.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

#include <stdint.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_block_query, BlockQuery,
                                     void (C::*)(block_info_t*, size_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_block_queue, BlockQueue, void (C::*)(block_op_t*));

template <typename D>
constexpr void CheckBlockProtocolSubclass() {
    static_assert(internal::has_block_query<D>::value,
                  "BlockProtocol subclasses must implement "
                  "BlockQuery(block_info_t* info_out, size_t* block_op_size_out)");
    static_assert(internal::has_block_queue<D>::value,
                  "BlockProtocol subclasses must implement "
                  "BlockQueue(block_op_t* txn)");
}

}  // namespace internal
}  // namespace ddk
