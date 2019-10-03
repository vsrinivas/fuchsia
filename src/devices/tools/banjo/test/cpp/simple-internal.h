// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.simple banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_draw, DrawingDraw,
        void (C::*)(const point_t* p, direction_t d));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_draw_lots, DrawingDrawLots,
        zx_status_t (C::*)(zx::vmo commands, point_t* out_p));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_draw_array, DrawingDrawArray,
        zx_status_t (C::*)(const point_t points[4]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_drawing_protocol_describe, DrawingDescribe,
        void (C::*)(const char* one, char* out_two, size_t two_capacity));


template <typename D>
constexpr void CheckDrawingProtocolSubclass() {
    static_assert(internal::has_drawing_protocol_draw<D>::value,
        "DrawingProtocol subclasses must implement "
        "void DrawingDraw(const point_t* p, direction_t d);");

    static_assert(internal::has_drawing_protocol_draw_lots<D>::value,
        "DrawingProtocol subclasses must implement "
        "zx_status_t DrawingDrawLots(zx::vmo commands, point_t* out_p);");

    static_assert(internal::has_drawing_protocol_draw_array<D>::value,
        "DrawingProtocol subclasses must implement "
        "zx_status_t DrawingDrawArray(const point_t points[4]);");

    static_assert(internal::has_drawing_protocol_describe<D>::value,
        "DrawingProtocol subclasses must implement "
        "void DrawingDescribe(const char* one, char* out_two, size_t two_capacity);");

}


} // namespace internal
} // namespace ddk
