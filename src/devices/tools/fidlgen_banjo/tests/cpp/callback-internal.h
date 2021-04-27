// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.callback banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_register_callback, DrawingRegisterCallback,
        void (C::*)(const draw_t* cb));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_deregister_callback, DrawingDeregisterCallback,
        void (C::*)());

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_register_callback2, DrawingRegisterCallback2,
        void (C::*)(const draw_callback_t* cb));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_draw_lots, DrawingDrawLots,
        int32_t (C::*)(zx::vmo commands, point_t* out_p));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_draw_array, DrawingDrawArray,
        zx_status_t (C::*)(const point_t points[4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_describe, DrawingDescribe,
        void (C::*)(const char* one, char* out_two, size_t two_capacity));


template <typename D>
constexpr void CheckDrawingProtocolSubclass() {
    static_assert(internal::has_drawing_protocol_register_callback<D>::value,
        "DrawingProtocol subclasses must implement "
        "void DrawingRegisterCallback(const draw_t* cb);");

    static_assert(internal::has_drawing_protocol_deregister_callback<D>::value,
        "DrawingProtocol subclasses must implement "
        "void DrawingDeregisterCallback();");

    static_assert(internal::has_drawing_protocol_register_callback2<D>::value,
        "DrawingProtocol subclasses must implement "
        "void DrawingRegisterCallback2(const draw_callback_t* cb);");

    static_assert(internal::has_drawing_protocol_draw_lots<D>::value,
        "DrawingProtocol subclasses must implement "
        "int32_t DrawingDrawLots(zx::vmo commands, point_t* out_p);");

    static_assert(internal::has_drawing_protocol_draw_array<D>::value,
        "DrawingProtocol subclasses must implement "
        "zx_status_t DrawingDrawArray(const point_t points[4]);");

    static_assert(internal::has_drawing_protocol_describe<D>::value,
        "DrawingProtocol subclasses must implement "
        "void DrawingDescribe(const char* one, char* out_two, size_t two_capacity);");

}


} // namespace internal
} // namespace ddk
