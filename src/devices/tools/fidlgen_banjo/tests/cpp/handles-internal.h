// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.handles banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_doer_protocol_do_something, DoerDoSomething,
        void (C::*)(zx::channel the_handle));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_doer_protocol_do_something_else, DoerDoSomethingElse,
        void (C::*)(zx::channel the_handle_too));


template <typename D>
constexpr void CheckDoerProtocolSubclass() {
    static_assert(internal::has_doer_protocol_do_something<D>::value,
        "DoerProtocol subclasses must implement "
        "void DoerDoSomething(zx::channel the_handle);");

    static_assert(internal::has_doer_protocol_do_something_else<D>::value,
        "DoerProtocol subclasses must implement "
        "void DoerDoSomethingElse(zx::channel the_handle_too);");

}


} // namespace internal
} // namespace ddk
