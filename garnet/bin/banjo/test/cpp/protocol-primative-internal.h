// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.primative banjo file

#pragma once

#include <banjo/examples/protocol/primative.h>
#include <type_traits>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_bool, SynchronousPrimativeBool,
        bool (C::*)(bool b, bool* out_b_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_int8, SynchronousPrimativeInt8,
        int8_t (C::*)(int8_t i8, int8_t* out_i8_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_int16, SynchronousPrimativeInt16,
        int16_t (C::*)(int16_t i16, int16_t* out_i16_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_int32, SynchronousPrimativeInt32,
        int32_t (C::*)(int32_t i32, int32_t* out_i32_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_int64, SynchronousPrimativeInt64,
        int64_t (C::*)(int64_t i64, int64_t* out_i64_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_uint8, SynchronousPrimativeUint8,
        uint8_t (C::*)(uint8_t u8, uint8_t* out_u8_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_uint16, SynchronousPrimativeUint16,
        uint16_t (C::*)(uint16_t u16, uint16_t* out_u16_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_uint32, SynchronousPrimativeUint32,
        uint32_t (C::*)(uint32_t u32, uint32_t* out_u32_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_uint64, SynchronousPrimativeUint64,
        uint64_t (C::*)(uint64_t u64, uint64_t* out_u64_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_float32, SynchronousPrimativeFloat32,
        float (C::*)(float f32, float* out_f32_2));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_synchronous_primative_protocol_float64, SynchronousPrimativeFloat64,
        double (C::*)(double u64, double* out_f64_2));


template <typename D>
constexpr void CheckSynchronousPrimativeProtocolSubclass() {
    static_assert(internal::has_synchronous_primative_protocol_bool<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "bool SynchronousPrimativeBool(bool b, bool* out_b_2);");

    static_assert(internal::has_synchronous_primative_protocol_int8<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "int8_t SynchronousPrimativeInt8(int8_t i8, int8_t* out_i8_2);");

    static_assert(internal::has_synchronous_primative_protocol_int16<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "int16_t SynchronousPrimativeInt16(int16_t i16, int16_t* out_i16_2);");

    static_assert(internal::has_synchronous_primative_protocol_int32<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "int32_t SynchronousPrimativeInt32(int32_t i32, int32_t* out_i32_2);");

    static_assert(internal::has_synchronous_primative_protocol_int64<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "int64_t SynchronousPrimativeInt64(int64_t i64, int64_t* out_i64_2);");

    static_assert(internal::has_synchronous_primative_protocol_uint8<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "uint8_t SynchronousPrimativeUint8(uint8_t u8, uint8_t* out_u8_2);");

    static_assert(internal::has_synchronous_primative_protocol_uint16<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "uint16_t SynchronousPrimativeUint16(uint16_t u16, uint16_t* out_u16_2);");

    static_assert(internal::has_synchronous_primative_protocol_uint32<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "uint32_t SynchronousPrimativeUint32(uint32_t u32, uint32_t* out_u32_2);");

    static_assert(internal::has_synchronous_primative_protocol_uint64<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "uint64_t SynchronousPrimativeUint64(uint64_t u64, uint64_t* out_u64_2);");

    static_assert(internal::has_synchronous_primative_protocol_float32<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "float SynchronousPrimativeFloat32(float f32, float* out_f32_2);");

    static_assert(internal::has_synchronous_primative_protocol_float64<D>::value,
        "SynchronousPrimativeProtocol subclasses must implement "
        "double SynchronousPrimativeFloat64(double u64, double* out_f64_2);");

}

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_bool, AsyncPrimativeBool,
        void (C::*)(bool b, async_primative_bool_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_int8, AsyncPrimativeInt8,
        void (C::*)(int8_t i8, async_primative_int8_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_int16, AsyncPrimativeInt16,
        void (C::*)(int16_t i16, async_primative_int16_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_int32, AsyncPrimativeInt32,
        void (C::*)(int32_t i32, async_primative_int32_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_int64, AsyncPrimativeInt64,
        void (C::*)(int64_t i64, async_primative_int64_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_uint8, AsyncPrimativeUint8,
        void (C::*)(uint8_t u8, async_primative_uint8_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_uint16, AsyncPrimativeUint16,
        void (C::*)(uint16_t u16, async_primative_uint16_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_uint32, AsyncPrimativeUint32,
        void (C::*)(uint32_t u32, async_primative_uint32_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_uint64, AsyncPrimativeUint64,
        void (C::*)(uint64_t u64, async_primative_uint64_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_float32, AsyncPrimativeFloat32,
        void (C::*)(float f32, async_primative_float32_callback callback, void* cookie));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_async_primative_protocol_float64, AsyncPrimativeFloat64,
        void (C::*)(double u64, async_primative_float64_callback callback, void* cookie));


template <typename D>
constexpr void CheckAsyncPrimativeProtocolSubclass() {
    static_assert(internal::has_async_primative_protocol_bool<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeBool(bool b, async_primative_bool_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_int8<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeInt8(int8_t i8, async_primative_int8_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_int16<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeInt16(int16_t i16, async_primative_int16_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_int32<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeInt32(int32_t i32, async_primative_int32_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_int64<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeInt64(int64_t i64, async_primative_int64_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_uint8<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeUint8(uint8_t u8, async_primative_uint8_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_uint16<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeUint16(uint16_t u16, async_primative_uint16_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_uint32<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeUint32(uint32_t u32, async_primative_uint32_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_uint64<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeUint64(uint64_t u64, async_primative_uint64_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_float32<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeFloat32(float f32, async_primative_float32_callback callback, void* cookie);");

    static_assert(internal::has_async_primative_protocol_float64<D>::value,
        "AsyncPrimativeProtocol subclasses must implement "
        "void AsyncPrimativeFloat64(double u64, async_primative_float64_callback callback, void* cookie);");

}

} // namespace internal
} // namespace ddk
