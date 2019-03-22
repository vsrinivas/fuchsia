// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example6 banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_hello_protocol_say, HelloSay,
        void (C::*)(const char* req, char* out_response, size_t response_capacity));


template <typename D>
constexpr void CheckHelloProtocolSubclass() {
    static_assert(internal::has_hello_protocol_say<D>::value,
        "HelloProtocol subclasses must implement "
        "void HelloSay(const char* req, char* out_response, size_t response_capacity);");

}


} // namespace internal
} // namespace ddk
