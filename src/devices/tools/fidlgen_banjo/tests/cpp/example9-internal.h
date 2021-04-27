// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example9 banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_echo_protocol_echo32, EchoEcho32,
        uint32_t (C::*)(uint32_t uint32));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_echo_protocol_echo64, EchoEcho64,
        uint64_t (C::*)(uint64_t uint64));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_echo_protocol_echo_enum, EchoEchoEnum,
        echo_me_t (C::*)(echo_me_t req));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_echo_protocol_echo_handle, EchoEchoHandle,
        void (C::*)(zx::handle req, zx::handle* out_response));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_echo_protocol_echo_channel, EchoEchoChannel,
        void (C::*)(zx::channel req, zx::channel* out_response));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_echo_protocol_echo_struct, EchoEchoStruct,
        void (C::*)(const echo_more_t* req, echo_more_t* out_response));


template <typename D>
constexpr void CheckEchoProtocolSubclass() {
    static_assert(internal::has_echo_protocol_echo32<D>::value,
        "EchoProtocol subclasses must implement "
        "uint32_t EchoEcho32(uint32_t uint32);");

    static_assert(internal::has_echo_protocol_echo64<D>::value,
        "EchoProtocol subclasses must implement "
        "uint64_t EchoEcho64(uint64_t uint64);");

    static_assert(internal::has_echo_protocol_echo_enum<D>::value,
        "EchoProtocol subclasses must implement "
        "echo_me_t EchoEchoEnum(echo_me_t req);");

    static_assert(internal::has_echo_protocol_echo_handle<D>::value,
        "EchoProtocol subclasses must implement "
        "void EchoEchoHandle(zx::handle req, zx::handle* out_response);");

    static_assert(internal::has_echo_protocol_echo_channel<D>::value,
        "EchoProtocol subclasses must implement "
        "void EchoEchoChannel(zx::channel req, zx::channel* out_response);");

    static_assert(internal::has_echo_protocol_echo_struct<D>::value,
        "EchoProtocol subclasses must implement "
        "void EchoEchoStruct(const echo_more_t* req, echo_more_t* out_response);");

}


} // namespace internal
} // namespace ddk
