// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.specialized.syscalls banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_create, Apicreate,
        zx_status_t (C::*)(zx::handle handle, int32_t options, const void buffer[buffer_size], size_t buffer_size));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_create<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apicreate(zx::handle handle, int32_t options, const void buffer[buffer_size], size_t buffer_size);");

}


} // namespace internal
} // namespace ddk
