// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <magenta/types.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

#include <stdint.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN(has_block_set_callbacks, BlockSetCallbacks);
DECLARE_HAS_MEMBER_FN(has_block_get_info, BlockGetInfo);
DECLARE_HAS_MEMBER_FN(has_block_read, BlockRead);
DECLARE_HAS_MEMBER_FN(has_block_write, BlockWrite);

template <typename D>
constexpr void CheckBlockProtocolSubclass() {
    static_assert(internal::has_block_set_callbacks<D>::value,
                  "BlockProtocol subclasses must implement BlockSetCallbacks");
    static_assert(fbl::is_same<decltype(&D::BlockSetCallbacks),
                                void (D::*)(block_callbacks_t*)>::value,
                  "BlockSetCallbacks must be a non-static member function with signature "
                  "'void BlockSetCallbacks(block_callbacks_t* cb)', and be visible to "
                  "ddk::BlockProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_block_get_info<D>::value,
                  "BlockProtocol subclasses must implement BlockGetInfo");
    static_assert(fbl::is_same<decltype(&D::BlockGetInfo),
                                void (D::*)(block_info_t*)>::value,
                  "BlockStop must be a non-static member function with signature "
                  "'void BlockGetInfo(block_info_t* info)', and be visible to "
                  "ddk::BlockProtocol<D> (either because they are public, or because "
                  "of friendship).");
    static_assert(internal::has_block_read<D>::value,
                  "BlockProtocol subclasses must implement BlockRead");
    static_assert(fbl::is_same<decltype(&D::BlockRead),
                                void (D::*)(mx_handle_t, uint64_t, uint64_t, uint64_t, void*)>::value,
                  "BlockRead must be a non-static member function with signature "
                  "'void BlockRead(mx_handle_t, uint64_t, uint64_t, uint64_t, void*)', and be "
                  "visible to ddk::BlockProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_block_write<D>::value,
                  "BlockProtocol subclasses must implement BlockWrite");
    static_assert(fbl::is_same<decltype(&D::BlockWrite),
                                void (D::*)(mx_handle_t, uint64_t, uint64_t, uint64_t, void*)>::value,
                  "BlockWrite must be a non-static member function with signature "
                  "'void BlockWrite(mx_handle_t, uint64_t, uint64_t, uint64_t, void*)', and be "
                  "visible to ddk::BlockProtocol<D> (either because they are public, or because of "
                  "friendship).");
}

}  // namespace internal
}  // namespace ddk
