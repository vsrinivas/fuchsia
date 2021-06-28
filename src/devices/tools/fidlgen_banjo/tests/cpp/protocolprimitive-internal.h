// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolprimitive banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_bool, SynchronousPrimitiveBool,
        bool (C::*)(bool b, bool* out_b_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_int8, SynchronousPrimitiveInt8,
        int8_t (C::*)(int8_t i8, int8_t* out_i8_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_int16, SynchronousPrimitiveInt16,
        int16_t (C::*)(int16_t i16, int16_t* out_i16_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_int32, SynchronousPrimitiveInt32,
        int32_t (C::*)(int32_t i32, int32_t* out_i32_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_int64, SynchronousPrimitiveInt64,
        int64_t (C::*)(int64_t i64, int64_t* out_i64_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_uint8, SynchronousPrimitiveUint8,
        uint8_t (C::*)(uint8_t u8, uint8_t* out_u8_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_uint16, SynchronousPrimitiveUint16,
        uint16_t (C::*)(uint16_t u16, uint16_t* out_u16_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_uint32, SynchronousPrimitiveUint32,
        uint32_t (C::*)(uint32_t u32, uint32_t* out_u32_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_uint64, SynchronousPrimitiveUint64,
        uint64_t (C::*)(uint64_t u64, uint64_t* out_u64_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_float32, SynchronousPrimitiveFloat32,
        float (C::*)(float f32, float* out_f32_2));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primitive_protocol_float64, SynchronousPrimitiveFloat64,
        double (C::*)(double u64, double* out_f64_2));


template <typename D>
constexpr void CheckSynchronousPrimitiveProtocolSubclass() {
    static_assert(internal::has_synchronous_primitive_protocol_bool<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "bool SynchronousPrimitiveBool(bool b, bool* out_b_2);");

    static_assert(internal::has_synchronous_primitive_protocol_int8<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "int8_t SynchronousPrimitiveInt8(int8_t i8, int8_t* out_i8_2);");

    static_assert(internal::has_synchronous_primitive_protocol_int16<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "int16_t SynchronousPrimitiveInt16(int16_t i16, int16_t* out_i16_2);");

    static_assert(internal::has_synchronous_primitive_protocol_int32<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "int32_t SynchronousPrimitiveInt32(int32_t i32, int32_t* out_i32_2);");

    static_assert(internal::has_synchronous_primitive_protocol_int64<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "int64_t SynchronousPrimitiveInt64(int64_t i64, int64_t* out_i64_2);");

    static_assert(internal::has_synchronous_primitive_protocol_uint8<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "uint8_t SynchronousPrimitiveUint8(uint8_t u8, uint8_t* out_u8_2);");

    static_assert(internal::has_synchronous_primitive_protocol_uint16<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "uint16_t SynchronousPrimitiveUint16(uint16_t u16, uint16_t* out_u16_2);");

    static_assert(internal::has_synchronous_primitive_protocol_uint32<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "uint32_t SynchronousPrimitiveUint32(uint32_t u32, uint32_t* out_u32_2);");

    static_assert(internal::has_synchronous_primitive_protocol_uint64<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "uint64_t SynchronousPrimitiveUint64(uint64_t u64, uint64_t* out_u64_2);");

    static_assert(internal::has_synchronous_primitive_protocol_float32<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "float SynchronousPrimitiveFloat32(float f32, float* out_f32_2);");

    static_assert(internal::has_synchronous_primitive_protocol_float64<D>::value,
        "SynchronousPrimitiveProtocol subclasses must implement "
        "double SynchronousPrimitiveFloat64(double u64, double* out_f64_2);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_bool, AsyncPrimitiveBool,
        void (C::*)(bool b, async_primitive_bool_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_int8, AsyncPrimitiveInt8,
        void (C::*)(int8_t i8, async_primitive_int8_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_int16, AsyncPrimitiveInt16,
        void (C::*)(int16_t i16, async_primitive_int16_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_int32, AsyncPrimitiveInt32,
        void (C::*)(int32_t i32, async_primitive_int32_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_int64, AsyncPrimitiveInt64,
        void (C::*)(int64_t i64, async_primitive_int64_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_uint8, AsyncPrimitiveUint8,
        void (C::*)(uint8_t u8, async_primitive_uint8_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_uint16, AsyncPrimitiveUint16,
        void (C::*)(uint16_t u16, async_primitive_uint16_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_uint32, AsyncPrimitiveUint32,
        void (C::*)(uint32_t u32, async_primitive_uint32_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_uint64, AsyncPrimitiveUint64,
        void (C::*)(uint64_t u64, async_primitive_uint64_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_float32, AsyncPrimitiveFloat32,
        void (C::*)(float f32, async_primitive_float32_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primitive_protocol_float64, AsyncPrimitiveFloat64,
        void (C::*)(double u64, async_primitive_float64_callback callback, void* cookie));


template <typename D>
constexpr void CheckAsyncPrimitiveProtocolSubclass() {
    static_assert(internal::has_async_primitive_protocol_bool<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveBool(bool b, async_primitive_bool_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_int8<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveInt8(int8_t i8, async_primitive_int8_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_int16<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveInt16(int16_t i16, async_primitive_int16_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_int32<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveInt32(int32_t i32, async_primitive_int32_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_int64<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveInt64(int64_t i64, async_primitive_int64_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_uint8<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveUint8(uint8_t u8, async_primitive_uint8_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_uint16<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveUint16(uint16_t u16, async_primitive_uint16_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_uint32<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveUint32(uint32_t u32, async_primitive_uint32_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_uint64<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveUint64(uint64_t u64, async_primitive_uint64_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_float32<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveFloat32(float f32, async_primitive_float32_callback callback, void* cookie);");

    static_assert(internal::has_async_primitive_protocol_float64<D>::value,
        "AsyncPrimitiveProtocol subclasses must implement "
        "void AsyncPrimitiveFloat64(double u64, async_primitive_float64_callback callback, void* cookie);");

}


} // namespace internal
} // namespace ddk
