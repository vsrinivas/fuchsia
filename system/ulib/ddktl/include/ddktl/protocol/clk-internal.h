// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clk_enable, ClkEnable,
        zx_status_t (C::*)(uint32_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clk_disable, ClkDisable,
        zx_status_t (C::*)(uint32_t));

template <typename D>
constexpr void CheckClkProtocolSubclass() {
    static_assert(internal::has_clk_enable<D>::value,
                  "ClkProtocol subclasses must implement "
                  "ClkEnable(uint32_t index)");
    static_assert(internal::has_clk_disable<D>::value,
                  "ClkProtocol subclasses must implement "
                  "ClkDisable(uint32_t index)");
 }

}  // namespace internal
}  // namespace ddk
