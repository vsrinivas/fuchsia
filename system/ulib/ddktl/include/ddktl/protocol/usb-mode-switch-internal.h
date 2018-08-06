// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_set_usb_mode, UmsSetMode,
        zx_status_t (C::*)(usb_mode_t));

template <typename D>
constexpr void CheckUmsProtocolSubclass() {
    static_assert(internal::has_set_usb_mode<D>::value,
                  "UmsProtocol subclasses must implement "
                  "UmsSetMode(usb_mode_t mode)");
 }

}  // namespace internal
}  // namespace ddk
