// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/clk.fidl INSTEAD.

#pragma once

#include <ddk/protocol/clk.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clk_protocol_enable, ClkEnable,
                                     zx_status_t (C::*)(uint32_t index));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clk_protocol_disable, ClkDisable,
                                     zx_status_t (C::*)(uint32_t index));

template <typename D>
constexpr void CheckClkProtocolSubclass() {
    static_assert(internal::has_clk_protocol_enable<D>::value,
                  "ClkProtocol subclasses must implement "
                  "zx_status_t ClkEnable(uint32_t index");
    static_assert(internal::has_clk_protocol_disable<D>::value,
                  "ClkProtocol subclasses must implement "
                  "zx_status_t ClkDisable(uint32_t index");
}

} // namespace internal
} // namespace ddk
