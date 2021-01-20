// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.references banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_in_out_protocol_protocol_do_something, InOutProtocolDoSomething,
        void (C::*)(some_type_t* param));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_in_out_protocol_protocol_do_some_other_thing, InOutProtocolDoSomeOtherThing,
        void (C::*)(some_type_t* param));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_in_out_protocol_protocol_do_some_default_thing, InOutProtocolDoSomeDefaultThing,
        void (C::*)(const some_type_t* param));


template <typename D>
constexpr void CheckInOutProtocolProtocolSubclass() {
    static_assert(internal::has_in_out_protocol_protocol_do_something<D>::value,
        "InOutProtocolProtocol subclasses must implement "
        "void InOutProtocolDoSomething(some_type_t* param);");

    static_assert(internal::has_in_out_protocol_protocol_do_some_other_thing<D>::value,
        "InOutProtocolProtocol subclasses must implement "
        "void InOutProtocolDoSomeOtherThing(some_type_t* param);");

    static_assert(internal::has_in_out_protocol_protocol_do_some_default_thing<D>::value,
        "InOutProtocolProtocol subclasses must implement "
        "void InOutProtocolDoSomeDefaultThing(const some_type_t* param);");

}


} // namespace internal
} // namespace ddk
