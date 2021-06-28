// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolarray banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_bool, ArrayofArraysBool,
        void (C::*)(const bool b[32][4], bool out_b[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_int8, ArrayofArraysInt8,
        void (C::*)(const int8_t i8[32][4], int8_t out_i8[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_int16, ArrayofArraysInt16,
        void (C::*)(const int16_t i16[32][4], int16_t out_i16[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_int32, ArrayofArraysInt32,
        void (C::*)(const int32_t i32[32][4], int32_t out_i32[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_int64, ArrayofArraysInt64,
        void (C::*)(const int64_t i64[32][4], int64_t out_i64[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_uint8, ArrayofArraysUint8,
        void (C::*)(const uint8_t u8[32][4], uint8_t out_u8[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_uint16, ArrayofArraysUint16,
        void (C::*)(const uint16_t u16[32][4], uint16_t out_u16[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_uint32, ArrayofArraysUint32,
        void (C::*)(const uint32_t u32[32][4], uint32_t out_u32[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_uint64, ArrayofArraysUint64,
        void (C::*)(const uint64_t u64[32][4], uint64_t out_u64[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_float32, ArrayofArraysFloat32,
        void (C::*)(const float f32[32][4], float out_f32[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_float64, ArrayofArraysFloat64,
        void (C::*)(const double u64[32][4], double out_f64[32][4]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_arrayof_arrays_protocol_handle, ArrayofArraysHandle,
        void (C::*)(const zx::handle u64[32][4], zx::handle out_f64[32][4]));


template <typename D>
constexpr void CheckArrayofArraysProtocolSubclass() {
    static_assert(internal::has_arrayof_arrays_protocol_bool<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysBool(const bool b[32][4], bool out_b[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_int8<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysInt8(const int8_t i8[32][4], int8_t out_i8[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_int16<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysInt16(const int16_t i16[32][4], int16_t out_i16[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_int32<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysInt32(const int32_t i32[32][4], int32_t out_i32[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_int64<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysInt64(const int64_t i64[32][4], int64_t out_i64[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_uint8<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysUint8(const uint8_t u8[32][4], uint8_t out_u8[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_uint16<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysUint16(const uint16_t u16[32][4], uint16_t out_u16[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_uint32<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysUint32(const uint32_t u32[32][4], uint32_t out_u32[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_uint64<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysUint64(const uint64_t u64[32][4], uint64_t out_u64[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_float32<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysFloat32(const float f32[32][4], float out_f32[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_float64<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysFloat64(const double u64[32][4], double out_f64[32][4]);");

    static_assert(internal::has_arrayof_arrays_protocol_handle<D>::value,
        "ArrayofArraysProtocol subclasses must implement "
        "void ArrayofArraysHandle(const zx::handle u64[32][4], zx::handle out_f64[32][4]);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_bool, ArrayBool,
        void (C::*)(const bool b[1], bool out_b[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_int8, ArrayInt8,
        void (C::*)(const int8_t i8[1], int8_t out_i8[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_int16, ArrayInt16,
        void (C::*)(const int16_t i16[1], int16_t out_i16[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_int32, ArrayInt32,
        void (C::*)(const int32_t i32[1], int32_t out_i32[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_int64, ArrayInt64,
        void (C::*)(const int64_t i64[1], int64_t out_i64[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_uint8, ArrayUint8,
        void (C::*)(const uint8_t u8[1], uint8_t out_u8[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_uint16, ArrayUint16,
        void (C::*)(const uint16_t u16[1], uint16_t out_u16[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_uint32, ArrayUint32,
        void (C::*)(const uint32_t u32[1], uint32_t out_u32[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_uint64, ArrayUint64,
        void (C::*)(const uint64_t u64[1], uint64_t out_u64[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_float32, ArrayFloat32,
        void (C::*)(const float f32[1], float out_f32[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_float64, ArrayFloat64,
        void (C::*)(const double u64[1], double out_f64[1]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array_protocol_handle, ArrayHandle,
        void (C::*)(const zx::handle u64[1], zx::handle out_f64[1]));


template <typename D>
constexpr void CheckArrayProtocolSubclass() {
    static_assert(internal::has_array_protocol_bool<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayBool(const bool b[1], bool out_b[1]);");

    static_assert(internal::has_array_protocol_int8<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayInt8(const int8_t i8[1], int8_t out_i8[1]);");

    static_assert(internal::has_array_protocol_int16<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayInt16(const int16_t i16[1], int16_t out_i16[1]);");

    static_assert(internal::has_array_protocol_int32<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayInt32(const int32_t i32[1], int32_t out_i32[1]);");

    static_assert(internal::has_array_protocol_int64<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayInt64(const int64_t i64[1], int64_t out_i64[1]);");

    static_assert(internal::has_array_protocol_uint8<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayUint8(const uint8_t u8[1], uint8_t out_u8[1]);");

    static_assert(internal::has_array_protocol_uint16<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayUint16(const uint16_t u16[1], uint16_t out_u16[1]);");

    static_assert(internal::has_array_protocol_uint32<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayUint32(const uint32_t u32[1], uint32_t out_u32[1]);");

    static_assert(internal::has_array_protocol_uint64<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayUint64(const uint64_t u64[1], uint64_t out_u64[1]);");

    static_assert(internal::has_array_protocol_float32<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayFloat32(const float f32[1], float out_f32[1]);");

    static_assert(internal::has_array_protocol_float64<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayFloat64(const double u64[1], double out_f64[1]);");

    static_assert(internal::has_array_protocol_handle<D>::value,
        "ArrayProtocol subclasses must implement "
        "void ArrayHandle(const zx::handle u64[1], zx::handle out_f64[1]);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_bool, Array2Bool,
        void (C::*)(const bool b[32], bool out_b[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_int8, Array2Int8,
        void (C::*)(const int8_t i8[32], int8_t out_i8[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_int16, Array2Int16,
        void (C::*)(const int16_t i16[32], int16_t out_i16[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_int32, Array2Int32,
        void (C::*)(const int32_t i32[32], int32_t out_i32[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_int64, Array2Int64,
        void (C::*)(const int64_t i64[32], int64_t out_i64[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_uint8, Array2Uint8,
        void (C::*)(const uint8_t u8[32], uint8_t out_u8[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_uint16, Array2Uint16,
        void (C::*)(const uint16_t u16[32], uint16_t out_u16[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_uint32, Array2Uint32,
        void (C::*)(const uint32_t u32[32], uint32_t out_u32[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_uint64, Array2Uint64,
        void (C::*)(const uint64_t u64[32], uint64_t out_u64[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_float32, Array2Float32,
        void (C::*)(const float f32[32], float out_f32[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_float64, Array2Float64,
        void (C::*)(const double u64[32], double out_f64[32]));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_array2_protocol_handle, Array2Handle,
        void (C::*)(const zx::handle u64[32], zx::handle out_f64[32]));


template <typename D>
constexpr void CheckArray2ProtocolSubclass() {
    static_assert(internal::has_array2_protocol_bool<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Bool(const bool b[32], bool out_b[32]);");

    static_assert(internal::has_array2_protocol_int8<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Int8(const int8_t i8[32], int8_t out_i8[32]);");

    static_assert(internal::has_array2_protocol_int16<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Int16(const int16_t i16[32], int16_t out_i16[32]);");

    static_assert(internal::has_array2_protocol_int32<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Int32(const int32_t i32[32], int32_t out_i32[32]);");

    static_assert(internal::has_array2_protocol_int64<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Int64(const int64_t i64[32], int64_t out_i64[32]);");

    static_assert(internal::has_array2_protocol_uint8<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Uint8(const uint8_t u8[32], uint8_t out_u8[32]);");

    static_assert(internal::has_array2_protocol_uint16<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Uint16(const uint16_t u16[32], uint16_t out_u16[32]);");

    static_assert(internal::has_array2_protocol_uint32<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Uint32(const uint32_t u32[32], uint32_t out_u32[32]);");

    static_assert(internal::has_array2_protocol_uint64<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Uint64(const uint64_t u64[32], uint64_t out_u64[32]);");

    static_assert(internal::has_array2_protocol_float32<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Float32(const float f32[32], float out_f32[32]);");

    static_assert(internal::has_array2_protocol_float64<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Float64(const double u64[32], double out_f64[32]);");

    static_assert(internal::has_array2_protocol_handle<D>::value,
        "Array2Protocol subclasses must implement "
        "void Array2Handle(const zx::handle u64[32], zx::handle out_f64[32]);");

}


} // namespace internal
} // namespace ddk
