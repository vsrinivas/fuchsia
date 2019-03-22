// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.libraryb banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_view_protocol_move_to, ViewMoveTo,
        void (C::*)(const point_t* p));


template <typename D>
constexpr void CheckViewProtocolSubclass() {
    static_assert(internal::has_view_protocol_move_to<D>::value,
        "ViewProtocol subclasses must implement "
        "void ViewMoveTo(const point_t* p);");

}


} // namespace internal
} // namespace ddk
