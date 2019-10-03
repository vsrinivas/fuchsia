// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example4 banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_interface_protocol_func, Interfacefunc,
        void (C::*)(bool x));


template <typename D>
constexpr void CheckInterfaceProtocolSubclass() {
    static_assert(internal::has_interface_protocol_func<D>::value,
        "InterfaceProtocol subclasses must implement "
        "void Interfacefunc(bool x);");

}


} // namespace internal
} // namespace ddk
