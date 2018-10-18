// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/block.banjo INSTEAD.

#pragma once

#include <ddk/protocol/block.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_block_impl_protocol_query, BlockImplQuery,
                                     void (C::*)(block_info_t* out_info,
                                                 size_t* out_block_op_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_block_impl_protocol_queue, BlockImplQueue,
                                     void (C::*)(block_op_t* txn,
                                                 block_impl_queue_callback callback, void* cookie));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_block_impl_protocol_get_stats, BlockImplGetStats,
                                     zx_status_t (C::*)(const void* cmd_buffer, size_t cmd_size,
                                                        void* out_reply_buffer, size_t reply_size,
                                                        size_t* out_reply_actual));

template <typename D>
constexpr void CheckBlockImplProtocolSubclass() {
    static_assert(internal::has_block_impl_protocol_query<D>::value,
                  "BlockImplProtocol subclasses must implement "
                  "void BlockImplQuery(block_info_t* out_info, size_t* out_block_op_size");
    static_assert(
        internal::has_block_impl_protocol_queue<D>::value,
        "BlockImplProtocol subclasses must implement "
        "void BlockImplQueue(block_op_t* txn, block_impl_queue_callback callback, void* cookie");
    static_assert(internal::has_block_impl_protocol_get_stats<D>::value,
                  "BlockImplProtocol subclasses must implement "
                  "zx_status_t BlockImplGetStats(const void* cmd_buffer, size_t cmd_size, void* "
                  "out_reply_buffer, size_t reply_size, size_t* out_reply_actual");
}

} // namespace internal
} // namespace ddk
