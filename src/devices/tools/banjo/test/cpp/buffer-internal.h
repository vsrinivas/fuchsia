// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.buffer banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_some_methods_protocol_do_something, SomeMethodsDoSomething,
        void (C::*)(const uint8_t* input_buffer, size_t input_size));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_some_methods_protocol_do_something_too, SomeMethodsDoSomethingToo,
        void (C::*)(const void* input_again_buffer, size_t input_again_size));


template <typename D>
constexpr void CheckSomeMethodsProtocolSubclass() {
    static_assert(internal::has_some_methods_protocol_do_something<D>::value,
        "SomeMethodsProtocol subclasses must implement "
        "void SomeMethodsDoSomething(const uint8_t* input_buffer, size_t input_size);");

    static_assert(internal::has_some_methods_protocol_do_something_too<D>::value,
        "SomeMethodsProtocol subclasses must implement "
        "void SomeMethodsDoSomethingToo(const void* input_again_buffer, size_t input_again_size);");

}


} // namespace internal
} // namespace ddk
