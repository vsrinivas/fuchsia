// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/display.h>
#include <ddktl/device-internal.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <stdint.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN(has_set_mode, SetMode);
DECLARE_HAS_MEMBER_FN(has_get_mode, GetMode);
DECLARE_HAS_MEMBER_FN(has_get_framebuffer, GetFramebuffer);
DECLARE_HAS_MEMBER_FN(has_flush, Flush);
DECLARE_HAS_MEMBER_FN(has_acquire_or_release_display, AcquireOrReleaseDisplay);
DECLARE_HAS_MEMBER_FN(has_set_ownership_change_callback, SetOwnershipChangeCallback);

template <typename D>
constexpr void CheckDisplayProtocolSubclass() {
    static_assert(internal::has_set_mode<D>::value,
                  "DisplayProtocol subclasses must implement SetMode");
    static_assert(fbl::is_same<decltype(&D::SetMode),
                                zx_status_t (D::*)(zx_display_info_t*)>::value,
                  "SetMode must be a non-static member function with signature "
                  "'zx_status_t SetMode(zx_display_info_t* info)', and be visible to "
                  "ddk::DisplayProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_get_mode<D>::value,
                  "DisplayProtocol subclasses must implement GetMode");
    static_assert(fbl::is_same<decltype(&D::GetMode),
                                zx_status_t (D::*)(zx_display_info_t*)>::value,
                  "GetMode must be a non-static member function with signature "
                  "'zx_status_t GetMode(zx_display_info_t* info)', and be visible to "
                  "ddk::DisplayProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_get_framebuffer<D>::value,
                  "DisplayProtocol subclasses must implement GetFramebuffer");
    static_assert(fbl::is_same<decltype(&D::GetFramebuffer),
                                zx_status_t (D::*)(void**)>::value,
                  "GetFramebuffer must be a non-static member function with signature "
                  "'zx_status_t GetFramebuffer(void** framebuffer)', and be visible to "
                  "ddk::DisplayProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_flush<D>::value,
                  "DisplayProtocol subclasses must implement Flush");
    static_assert(fbl::is_same<decltype(&D::Flush),
                                void (D::*)()>::value,
                  "Flush must be a non-static member function with signature "
                  "'void Flush()', and be visible to "
                  "ddk::DisplayProtocol<D> (either because they are public, or because of "
                  "friendship).");
}

}  // namespace internal
}  // namespace ddk
