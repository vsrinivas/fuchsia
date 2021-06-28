// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolvector banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_bool, VectorOfVectorsBool,
        void (C::*)(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_int8, VectorOfVectorsInt8,
        void (C::*)(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_int16, VectorOfVectorsInt16,
        void (C::*)(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_int32, VectorOfVectorsInt32,
        void (C::*)(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_int64, VectorOfVectorsInt64,
        void (C::*)(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_uint8, VectorOfVectorsUint8,
        void (C::*)(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_uint16, VectorOfVectorsUint16,
        void (C::*)(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_uint32, VectorOfVectorsUint32,
        void (C::*)(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_uint64, VectorOfVectorsUint64,
        void (C::*)(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_float32, VectorOfVectorsFloat32,
        void (C::*)(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_float64, VectorOfVectorsFloat64,
        void (C::*)(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_of_vectors_protocol_handle, VectorOfVectorsHandle,
        void (C::*)(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual));


template <typename D>
constexpr void CheckVectorOfVectorsProtocolSubclass() {
    static_assert(internal::has_vector_of_vectors_protocol_bool<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsBool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_int8<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsInt8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_int16<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsInt16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_int32<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsInt32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_int64<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsInt64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_uint8<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsUint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_uint16<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsUint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_uint32<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsUint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_uint64<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsUint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_float32<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsFloat32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_float64<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsFloat64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);");

    static_assert(internal::has_vector_of_vectors_protocol_handle<D>::value,
        "VectorOfVectorsProtocol subclasses must implement "
        "void VectorOfVectorsHandle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_bool, VectorBool,
        void (C::*)(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_int8, VectorInt8,
        void (C::*)(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_int16, VectorInt16,
        void (C::*)(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_int32, VectorInt32,
        void (C::*)(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_int64, VectorInt64,
        void (C::*)(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_uint8, VectorUint8,
        void (C::*)(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_uint16, VectorUint16,
        void (C::*)(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_uint32, VectorUint32,
        void (C::*)(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_uint64, VectorUint64,
        void (C::*)(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_float32, VectorFloat32,
        void (C::*)(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_float64, VectorFloat64,
        void (C::*)(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector_protocol_handle, VectorHandle,
        void (C::*)(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual));


template <typename D>
constexpr void CheckVectorProtocolSubclass() {
    static_assert(internal::has_vector_protocol_bool<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorBool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);");

    static_assert(internal::has_vector_protocol_int8<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorInt8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);");

    static_assert(internal::has_vector_protocol_int16<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorInt16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);");

    static_assert(internal::has_vector_protocol_int32<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorInt32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);");

    static_assert(internal::has_vector_protocol_int64<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorInt64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);");

    static_assert(internal::has_vector_protocol_uint8<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorUint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);");

    static_assert(internal::has_vector_protocol_uint16<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorUint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);");

    static_assert(internal::has_vector_protocol_uint32<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorUint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);");

    static_assert(internal::has_vector_protocol_uint64<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorUint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);");

    static_assert(internal::has_vector_protocol_float32<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorFloat32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);");

    static_assert(internal::has_vector_protocol_float64<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorFloat64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);");

    static_assert(internal::has_vector_protocol_handle<D>::value,
        "VectorProtocol subclasses must implement "
        "void VectorHandle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_bool, Vector2Bool,
        void (C::*)(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_int8, Vector2Int8,
        void (C::*)(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_int16, Vector2Int16,
        void (C::*)(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_int32, Vector2Int32,
        void (C::*)(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_int64, Vector2Int64,
        void (C::*)(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_uint8, Vector2Uint8,
        void (C::*)(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_uint16, Vector2Uint16,
        void (C::*)(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_uint32, Vector2Uint32,
        void (C::*)(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_uint64, Vector2Uint64,
        void (C::*)(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_float32, Vector2Float32,
        void (C::*)(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_float64, Vector2Float64,
        void (C::*)(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_vector2_protocol_handle, Vector2Handle,
        void (C::*)(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual));


template <typename D>
constexpr void CheckVector2ProtocolSubclass() {
    static_assert(internal::has_vector2_protocol_bool<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Bool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);");

    static_assert(internal::has_vector2_protocol_int8<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Int8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);");

    static_assert(internal::has_vector2_protocol_int16<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Int16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);");

    static_assert(internal::has_vector2_protocol_int32<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Int32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);");

    static_assert(internal::has_vector2_protocol_int64<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Int64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);");

    static_assert(internal::has_vector2_protocol_uint8<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Uint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);");

    static_assert(internal::has_vector2_protocol_uint16<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Uint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);");

    static_assert(internal::has_vector2_protocol_uint32<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Uint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);");

    static_assert(internal::has_vector2_protocol_uint64<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Uint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);");

    static_assert(internal::has_vector2_protocol_float32<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Float32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);");

    static_assert(internal::has_vector2_protocol_float64<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Float64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);");

    static_assert(internal::has_vector2_protocol_handle<D>::value,
        "Vector2Protocol subclasses must implement "
        "void Vector2Handle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);");

}


} // namespace internal
} // namespace ddk
