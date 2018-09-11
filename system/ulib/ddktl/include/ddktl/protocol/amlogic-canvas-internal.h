// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/amlogic_canvas.banjo INSTEAD.

#pragma once

#include <ddk/protocol/amlogic-canvas.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_canvas_protocol_config, CanvasConfig,
                                     zx_status_t (C::*)(zx_handle_t vmo, size_t offset,
                                                        const canvas_info_t* info,
                                                        uint8_t* out_canvas_idx));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_canvas_protocol_free, CanvasFree,
                                     zx_status_t (C::*)(uint8_t canvas_idx));

template <typename D>
constexpr void CheckCanvasProtocolSubclass() {
    static_assert(internal::has_canvas_protocol_config<D>::value,
                  "CanvasProtocol subclasses must implement "
                  "zx_status_t CanvasConfig(zx_handle_t vmo, size_t offset, const canvas_info_t* "
                  "info, uint8_t* out_canvas_idx");
    static_assert(internal::has_canvas_protocol_free<D>::value,
                  "CanvasProtocol subclasses must implement "
                  "zx_status_t CanvasFree(uint8_t canvas_idx");
}

} // namespace internal
} // namespace ddk
