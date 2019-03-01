// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.interface banjo file

#pragma once

#include <banjo/examples/interface.h>
#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_baker_protocol_register, BakerRegister,
        void (C::*)(const cookie_maker_t* intf));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_baker_protocol_de_register, BakerDeRegister,
        void (C::*)());


template <typename D>
constexpr void CheckBakerProtocolSubclass() {
    static_assert(internal::has_baker_protocol_register<D>::value,
        "BakerProtocol subclasses must implement "
        "void BakerRegister(const cookie_maker_t* intf);");

    static_assert(internal::has_baker_protocol_de_register<D>::value,
        "BakerProtocol subclasses must implement "
        "void BakerDeRegister();");

}

} // namespace internal
} // namespace ddk
