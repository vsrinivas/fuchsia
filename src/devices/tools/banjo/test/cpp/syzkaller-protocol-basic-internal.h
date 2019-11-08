// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.basic banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_usize, ApiUsize,
        size_t (C::*)(size_t sz));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bool, ApiBool,
        bool (C::*)(bool b));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int8, ApiInt8,
        int8_t (C::*)(int8_t i8));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int16, ApiInt16,
        int16_t (C::*)(int16_t i16));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int32, ApiInt32,
        int32_t (C::*)(int32_t i32));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int64, ApiInt64,
        int64_t (C::*)(int64_t i64));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint8, ApiUint8,
        uint8_t (C::*)(uint8_t u8));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint16, ApiUint16,
        uint16_t (C::*)(uint16_t u16));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint32, ApiUint32,
        uint32_t (C::*)(uint32_t u32));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint64, ApiUint64,
        uint64_t (C::*)(uint64_t u64));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle, ApiHandle,
        void (C::*)(zx::handle h));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_usize<D>::value,
        "ApiProtocol subclasses must implement "
        "size_t ApiUsize(size_t sz);");

    static_assert(internal::has_api_protocol_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "bool ApiBool(bool b);");

    static_assert(internal::has_api_protocol_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "int8_t ApiInt8(int8_t i8);");

    static_assert(internal::has_api_protocol_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "int16_t ApiInt16(int16_t i16);");

    static_assert(internal::has_api_protocol_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "int32_t ApiInt32(int32_t i32);");

    static_assert(internal::has_api_protocol_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "int64_t ApiInt64(int64_t i64);");

    static_assert(internal::has_api_protocol_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "uint8_t ApiUint8(uint8_t u8);");

    static_assert(internal::has_api_protocol_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "uint16_t ApiUint16(uint16_t u16);");

    static_assert(internal::has_api_protocol_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "uint32_t ApiUint32(uint32_t u32);");

    static_assert(internal::has_api_protocol_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "uint64_t ApiUint64(uint64_t u64);");

    static_assert(internal::has_api_protocol_handle<D>::value,
        "ApiProtocol subclasses must implement "
        "void ApiHandle(zx::handle h);");

}


} // namespace internal
} // namespace ddk
