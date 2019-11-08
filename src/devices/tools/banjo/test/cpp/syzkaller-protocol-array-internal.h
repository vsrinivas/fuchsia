// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.array banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_void_ptr, ApiVoidPtr,
        zx_status_t (C::*)(const void vptr[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_void_ptr, ApiVoidPtr,
        zx_status_t (C::*)(const void vptr[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_void_ptr, ApiVoidPtr,
        zx_status_t (C::*)(const void vptr[vptr_len], size_t vptr_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_void_ptr, ApiVoidPtr,
        zx_status_t (C::*)(const void vptr[vptr_len], size_t vptr_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_usize, ApiUsize,
        zx_status_t (C::*)(const size_t sz[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_usize, ApiUsize,
        zx_status_t (C::*)(const size_t sz[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_usize, ApiUsize,
        zx_status_t (C::*)(const size_t sz[sz_len], size_t sz_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_usize, ApiUsize,
        zx_status_t (C::*)(const size_t sz[sz_len], size_t sz_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bool, ApiBool,
        zx_status_t (C::*)(const bool b[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bool, ApiBool,
        zx_status_t (C::*)(const bool b[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bool, ApiBool,
        zx_status_t (C::*)(const bool b[b_len], size_t b_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bool, ApiBool,
        zx_status_t (C::*)(const bool b[b_len], size_t b_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int8, ApiInt8,
        zx_status_t (C::*)(const int8_t i8[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int8, ApiInt8,
        zx_status_t (C::*)(const int8_t i8[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int8, ApiInt8,
        zx_status_t (C::*)(const int8_t i8[i8_len], size_t i8_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int8, ApiInt8,
        zx_status_t (C::*)(const int8_t i8[i8_len], size_t i8_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int16, ApiInt16,
        zx_status_t (C::*)(const int16_t i16[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int16, ApiInt16,
        zx_status_t (C::*)(const int16_t i16[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int16, ApiInt16,
        zx_status_t (C::*)(const int16_t i16[i16_len], size_t i16_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int16, ApiInt16,
        zx_status_t (C::*)(const int16_t i16[i16_len], size_t i16_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int32, ApiInt32,
        zx_status_t (C::*)(const int32_t i32[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int32, ApiInt32,
        zx_status_t (C::*)(const int32_t i32[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int32, ApiInt32,
        zx_status_t (C::*)(const int32_t i32[i32_len], size_t i32_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int32, ApiInt32,
        zx_status_t (C::*)(const int32_t i32[i32_len], size_t i32_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int64, ApiInt64,
        zx_status_t (C::*)(const int64_t i64[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int64, ApiInt64,
        zx_status_t (C::*)(const int64_t i64[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int64, ApiInt64,
        zx_status_t (C::*)(const int64_t i64[i64_len], size_t i64_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int64, ApiInt64,
        zx_status_t (C::*)(const int64_t i64[i64_len], size_t i64_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint8, ApiUint8,
        zx_status_t (C::*)(const uint8_t u8[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint8, ApiUint8,
        zx_status_t (C::*)(const uint8_t u8[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint8, ApiUint8,
        zx_status_t (C::*)(const uint8_t u8[u8_len], size_t u8_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint8, ApiUint8,
        zx_status_t (C::*)(const uint8_t u8[u8_len], size_t u8_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint16, ApiUint16,
        zx_status_t (C::*)(const uint16_t u16[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint16, ApiUint16,
        zx_status_t (C::*)(const uint16_t u16[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint16, ApiUint16,
        zx_status_t (C::*)(const uint16_t u16[u16_len], size_t u16_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint16, ApiUint16,
        zx_status_t (C::*)(const uint16_t u16[u16_len], size_t u16_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint32, ApiUint32,
        zx_status_t (C::*)(const uint32_t u32[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint32, ApiUint32,
        zx_status_t (C::*)(const uint32_t u32[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint32, ApiUint32,
        zx_status_t (C::*)(const uint32_t u32[u32_len], size_t u32_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint32, ApiUint32,
        zx_status_t (C::*)(const uint32_t u32[u32_len], size_t u32_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint64, ApiUint64,
        zx_status_t (C::*)(const uint64_t u64[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint64, ApiUint64,
        zx_status_t (C::*)(const uint64_t u64[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint64, ApiUint64,
        zx_status_t (C::*)(const uint64_t u64[u64_len], size_t u64_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint64, ApiUint64,
        zx_status_t (C::*)(const uint64_t u64[u64_len], size_t u64_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle, ApiHandle,
        zx_status_t (C::*)(const zx::handle h[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle, ApiHandle,
        zx_status_t (C::*)(const zx::handle h[1]));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle, ApiHandle,
        zx_status_t (C::*)(const zx::handle h[h_len], size_t h_len));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_handle, ApiHandle,
        zx_status_t (C::*)(const zx::handle h[h_len], size_t h_len));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_void_ptr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiVoidPtr(const void vptr[1]);");

    static_assert(internal::has_api_protocol_void_ptr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiVoidPtr(const void vptr[1]);");

    static_assert(internal::has_api_protocol_void_ptr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiVoidPtr(const void vptr[vptr_len], size_t vptr_len);");

    static_assert(internal::has_api_protocol_void_ptr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiVoidPtr(const void vptr[vptr_len], size_t vptr_len);");

    static_assert(internal::has_api_protocol_usize<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUsize(const size_t sz[1]);");

    static_assert(internal::has_api_protocol_usize<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUsize(const size_t sz[1]);");

    static_assert(internal::has_api_protocol_usize<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUsize(const size_t sz[sz_len], size_t sz_len);");

    static_assert(internal::has_api_protocol_usize<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUsize(const size_t sz[sz_len], size_t sz_len);");

    static_assert(internal::has_api_protocol_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiBool(const bool b[1]);");

    static_assert(internal::has_api_protocol_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiBool(const bool b[1]);");

    static_assert(internal::has_api_protocol_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiBool(const bool b[b_len], size_t b_len);");

    static_assert(internal::has_api_protocol_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiBool(const bool b[b_len], size_t b_len);");

    static_assert(internal::has_api_protocol_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt8(const int8_t i8[1]);");

    static_assert(internal::has_api_protocol_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt8(const int8_t i8[1]);");

    static_assert(internal::has_api_protocol_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt8(const int8_t i8[i8_len], size_t i8_len);");

    static_assert(internal::has_api_protocol_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt8(const int8_t i8[i8_len], size_t i8_len);");

    static_assert(internal::has_api_protocol_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt16(const int16_t i16[1]);");

    static_assert(internal::has_api_protocol_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt16(const int16_t i16[1]);");

    static_assert(internal::has_api_protocol_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt16(const int16_t i16[i16_len], size_t i16_len);");

    static_assert(internal::has_api_protocol_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt16(const int16_t i16[i16_len], size_t i16_len);");

    static_assert(internal::has_api_protocol_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt32(const int32_t i32[1]);");

    static_assert(internal::has_api_protocol_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt32(const int32_t i32[1]);");

    static_assert(internal::has_api_protocol_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt32(const int32_t i32[i32_len], size_t i32_len);");

    static_assert(internal::has_api_protocol_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt32(const int32_t i32[i32_len], size_t i32_len);");

    static_assert(internal::has_api_protocol_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt64(const int64_t i64[1]);");

    static_assert(internal::has_api_protocol_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt64(const int64_t i64[1]);");

    static_assert(internal::has_api_protocol_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt64(const int64_t i64[i64_len], size_t i64_len);");

    static_assert(internal::has_api_protocol_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiInt64(const int64_t i64[i64_len], size_t i64_len);");

    static_assert(internal::has_api_protocol_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint8(const uint8_t u8[1]);");

    static_assert(internal::has_api_protocol_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint8(const uint8_t u8[1]);");

    static_assert(internal::has_api_protocol_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint8(const uint8_t u8[u8_len], size_t u8_len);");

    static_assert(internal::has_api_protocol_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint8(const uint8_t u8[u8_len], size_t u8_len);");

    static_assert(internal::has_api_protocol_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint16(const uint16_t u16[1]);");

    static_assert(internal::has_api_protocol_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint16(const uint16_t u16[1]);");

    static_assert(internal::has_api_protocol_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint16(const uint16_t u16[u16_len], size_t u16_len);");

    static_assert(internal::has_api_protocol_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint16(const uint16_t u16[u16_len], size_t u16_len);");

    static_assert(internal::has_api_protocol_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint32(const uint32_t u32[1]);");

    static_assert(internal::has_api_protocol_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint32(const uint32_t u32[1]);");

    static_assert(internal::has_api_protocol_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint32(const uint32_t u32[u32_len], size_t u32_len);");

    static_assert(internal::has_api_protocol_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint32(const uint32_t u32[u32_len], size_t u32_len);");

    static_assert(internal::has_api_protocol_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint64(const uint64_t u64[1]);");

    static_assert(internal::has_api_protocol_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint64(const uint64_t u64[1]);");

    static_assert(internal::has_api_protocol_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint64(const uint64_t u64[u64_len], size_t u64_len);");

    static_assert(internal::has_api_protocol_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiUint64(const uint64_t u64[u64_len], size_t u64_len);");

    static_assert(internal::has_api_protocol_handle<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiHandle(const zx::handle h[1]);");

    static_assert(internal::has_api_protocol_handle<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiHandle(const zx::handle h[1]);");

    static_assert(internal::has_api_protocol_handle<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiHandle(const zx::handle h[h_len], size_t h_len);");

    static_assert(internal::has_api_protocol_handle<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t ApiHandle(const zx::handle h[h_len], size_t h_len);");

}


} // namespace internal
} // namespace ddk
