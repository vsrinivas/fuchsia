// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/nand.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_query, Query,
                                     void (C::*)(nand_info_t*, size_t*));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_queue, Queue, void (C::*)(nand_op_t*));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_get_factory_bad_block_list, GetFactoryBadBlockList,
                                     zx_status_t (C::*)(uint32_t* , uint32_t , uint32_t*));

template <typename D>
constexpr void CheckNandProtocolSubclass() {
    static_assert(internal::has_query<D>::value,
                  "NandProtocol subclasses must implement "
                  "Query(nand_info_t* info_out, size_t* nand_op_size_out)");
    static_assert(internal::has_queue<D>::value,
                  "NandProtocol subclasses must implement "
                  "Queue(nand_op_t* operation)");
    static_assert(internal::has_get_factory_bad_block_list<D>::value,
                  "NandProtocol subclasses must implement "
                  "GetFactoryBadBlockList(uint32_t* bad_blocks, "
                  "uint32_t bad_block_len, uint32_t* num_bad_blocks)");
}

}  // namespace internal
}  // namespace ddk
