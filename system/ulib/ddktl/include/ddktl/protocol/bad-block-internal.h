// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/bad-block.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_get_bad_block_list2, GetBadBlockList,
                                     zx_status_t (C::*)(uint32_t*, uint32_t, uint32_t*));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_mark_block_bad, MarkBlockBad,
                                     zx_status_t (C::*)(uint32_t));

template <typename D>
constexpr void CheckBadBlockable() {
    static_assert(internal::has_get_bad_block_list2<D>::value,
                  "BadBlockProtocol subclasses must implement "
                  "GetBadBlockList(uint32_t* bad_blocks, "
                  "uint32_t bad_block_len, uint32_t* num_bad_blocks)");
    static_assert(internal::has_mark_block_bad<D>::value,
                  "BadBlockProtocol subclasses must implement "
                  "MarkBlockBad(uint32_t block)");
}

} // namespace internal
} // namespace ddk
