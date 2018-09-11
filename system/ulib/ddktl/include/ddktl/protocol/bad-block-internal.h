// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bad_block.banjo INSTEAD.

#pragma once

#include <ddk/protocol/bad-block.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_bad_block_protocol_get_bad_block_list,
                                     BadBlockGetBadBlockList,
                                     zx_status_t (C::*)(uint32_t* out_bad_blocks_list,
                                                        size_t bad_blocks_count,
                                                        size_t* out_bad_blocks_actual));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_bad_block_protocol_mark_block_bad, BadBlockMarkBlockBad,
                                     zx_status_t (C::*)(uint32_t block));

template <typename D>
constexpr void CheckBadBlockProtocolSubclass() {
    static_assert(internal::has_bad_block_protocol_get_bad_block_list<D>::value,
                  "BadBlockProtocol subclasses must implement "
                  "zx_status_t BadBlockGetBadBlockList(uint32_t* out_bad_blocks_list, size_t "
                  "bad_blocks_count, size_t* out_bad_blocks_actual");
    static_assert(internal::has_bad_block_protocol_mark_block_bad<D>::value,
                  "BadBlockProtocol subclasses must implement "
                  "zx_status_t BadBlockMarkBlockBad(uint32_t block");
}

} // namespace internal
} // namespace ddk
