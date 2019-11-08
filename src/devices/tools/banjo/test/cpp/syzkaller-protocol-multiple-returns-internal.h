// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.multiple.returns banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_usize, ApiUsize,
        zx_status_t (C::*)(size_t sz, size_t* out_sz_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bool, ApiBool,
        zx_status_t (C::*)(bool b, bool* out_b_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int8, ApiInt8,
        zx_status_t (C::*)(int8_t i8, int8_t* out_i8_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int16, ApiInt16,
        zx_status_t (C::*)(int16_t i16, int16_t* out_i16_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int32, ApiInt32,
        zx_status_t (C::*)(int32_t i32, int32_t* out_i32_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int64, ApiInt64,
        zx_status_t (C::*)(int64_t i64, int64_t* out_i64_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint8, ApiUint8,
        zx_status_t (C::*)(uint8_t u8, uint8_t* out_u8_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint16, ApiUint16,
        zx_status_t (C::*)(uint16_t u16, uint16_t* out_u16_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint32, ApiUint32,
        zx_status_t (C::*)(uint32_t u32, uint32_t* out_u32_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint64, ApiUint64,
        zx_status_t (C::*)(uint64_t u64, uint64_t* out_u64_1));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle, ApiHandle,
        zx_status_t (C::*)(zx::handle h, zx::handle* out_h_1));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_usize<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUsize(size_t sz, size_t* out_sz_1);");

    static_assert(internal::has_api_protocol_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiBool(bool b, bool* out_b_1);");

    static_assert(internal::has_api_protocol_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt8(int8_t i8, int8_t* out_i8_1);");

    static_assert(internal::has_api_protocol_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt16(int16_t i16, int16_t* out_i16_1);");

    static_assert(internal::has_api_protocol_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt32(int32_t i32, int32_t* out_i32_1);");

    static_assert(internal::has_api_protocol_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt64(int64_t i64, int64_t* out_i64_1);");

    static_assert(internal::has_api_protocol_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint8(uint8_t u8, uint8_t* out_u8_1);");

    static_assert(internal::has_api_protocol_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint16(uint16_t u16, uint16_t* out_u16_1);");

    static_assert(internal::has_api_protocol_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint32(uint32_t u32, uint32_t* out_u32_1);");

    static_assert(internal::has_api_protocol_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint64(uint64_t u64, uint64_t* out_u64_1);");

    static_assert(internal::has_api_protocol_handle<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiHandle(zx::handle h, zx::handle* out_h_1);");

}


} // namespace internal
} // namespace ddk
