// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_canvas_config, CanvasConfig,
        zx_status_t (C::*)(zx_handle_t, size_t, canvas_info_t*, uint8_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_canvas_free, CanvasFree,
        zx_status_t (C::*)(uint8_t));

template <typename D>
constexpr void CheckCanvasProtocolSubclass() {
    static_assert(internal::has_canvas_config<D>::value,
                  "CanvasProtocol subclasses must implement "
                  "CanvasConfig(zx_handle_t vmo, size_t offset, canvas_info_t* info, "
                  "uint8_t* canvas_idx)");
    static_assert(internal::has_canvas_free<D>::value,
                  "CanvasProtocol subclasses must implement "
                  "CanvasFree(uint8_t canvas_idx)");
 }

}  // namespace internal
}  // namespace ddk
