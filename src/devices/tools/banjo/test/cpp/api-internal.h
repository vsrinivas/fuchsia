// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.api banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_bool, Apibool,
        zx_status_t (C::*)(zx::handle handle, bool data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int8, Apiint8,
        zx_status_t (C::*)(zx::handle handle, int8_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int16, Apiint16,
        zx_status_t (C::*)(zx::handle handle, int16_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int32, Apiint32,
        zx_status_t (C::*)(zx::handle handle, int32_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_int64, Apiint64,
        zx_status_t (C::*)(zx::handle handle, int64_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint8, Apiuint8,
        zx_status_t (C::*)(zx::handle handle, uint8_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint16, Apiuint16,
        zx_status_t (C::*)(zx::handle handle, uint16_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint32, Apiuint32,
        zx_status_t (C::*)(zx::handle handle, uint32_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_uint64, Apiuint64,
        zx_status_t (C::*)(zx::handle handle, uint64_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_float32, Apifloat32,
        zx_status_t (C::*)(zx::handle handle, float data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_float64, Apifloat64,
        zx_status_t (C::*)(zx::handle handle, double data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_duration, Apiduration,
        zx_status_t (C::*)(zx::handle handle, zx_duration_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_koid, Apikoid,
        zx_status_t (C::*)(zx::handle handle, zx_koid_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_paddr, Apipaddr,
        zx_status_t (C::*)(zx::handle handle, zx_paddr_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_signals, Apisignals,
        zx_status_t (C::*)(zx::handle handle, zx_signals_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_time, Apitime,
        zx_status_t (C::*)(zx::handle handle, zx_time_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_vaddr, Apivaddr,
        zx_status_t (C::*)(zx::handle handle, zx_vaddr_t data));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_bool, Apioutput_bool,
        zx_status_t (C::*)(zx::handle handle, bool* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_int8, Apioutput_int8,
        zx_status_t (C::*)(zx::handle handle, int8_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_int16, Apioutput_int16,
        zx_status_t (C::*)(zx::handle handle, int16_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_int32, Apioutput_int32,
        zx_status_t (C::*)(zx::handle handle, int32_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_int64, Apioutput_int64,
        zx_status_t (C::*)(zx::handle handle, int64_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_uint8, Apioutput_uint8,
        zx_status_t (C::*)(zx::handle handle, uint8_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_uint16, Apioutput_uint16,
        zx_status_t (C::*)(zx::handle handle, uint16_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_uint32, Apioutput_uint32,
        zx_status_t (C::*)(zx::handle handle, uint32_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_uint64, Apioutput_uint64,
        zx_status_t (C::*)(zx::handle handle, uint64_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_float32, Apioutput_float32,
        zx_status_t (C::*)(zx::handle handle, float* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_float64, Apioutput_float64,
        zx_status_t (C::*)(zx::handle handle, double* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_duration, Apioutput_duration,
        zx_status_t (C::*)(zx::handle handle, zx_duration_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_koid, Apioutput_koid,
        zx_status_t (C::*)(zx::handle handle, zx_koid_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_paddr, Apioutput_paddr,
        zx_status_t (C::*)(zx::handle handle, zx_paddr_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_signals, Apioutput_signals,
        zx_status_t (C::*)(zx::handle handle, zx_signals_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_time, Apioutput_time,
        zx_status_t (C::*)(zx::handle handle, zx_time_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_output_vaddr, Apioutput_vaddr,
        zx_status_t (C::*)(zx::handle handle, zx_vaddr_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_return_void, Apireturn_void,
        void (C::*)(zx::handle handle));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_return_status, Apireturn_status,
        zx_status_t (C::*)(zx::handle handle));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_return_ticks, Apireturn_ticks,
        zx_ticks_t (C::*)(zx::handle handle));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_return_time, Apireturn_time,
        zx_time_t (C::*)(zx::handle handle));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_return_uint32, Apireturn_uint32,
        uint32_t (C::*)(zx::handle handle));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_api_protocol_return_uint64, Apireturn_uint64,
        uint64_t (C::*)(zx::handle handle));


template <typename D>
constexpr void CheckApiProtocolSubclass() {
    static_assert(internal::has_api_protocol_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apibool(zx::handle handle, bool data);");

    static_assert(internal::has_api_protocol_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiint8(zx::handle handle, int8_t data);");

    static_assert(internal::has_api_protocol_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiint16(zx::handle handle, int16_t data);");

    static_assert(internal::has_api_protocol_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiint32(zx::handle handle, int32_t data);");

    static_assert(internal::has_api_protocol_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiint64(zx::handle handle, int64_t data);");

    static_assert(internal::has_api_protocol_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiuint8(zx::handle handle, uint8_t data);");

    static_assert(internal::has_api_protocol_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiuint16(zx::handle handle, uint16_t data);");

    static_assert(internal::has_api_protocol_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiuint32(zx::handle handle, uint32_t data);");

    static_assert(internal::has_api_protocol_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiuint64(zx::handle handle, uint64_t data);");

    static_assert(internal::has_api_protocol_float32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifloat32(zx::handle handle, float data);");

    static_assert(internal::has_api_protocol_float64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apifloat64(zx::handle handle, double data);");

    static_assert(internal::has_api_protocol_duration<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apiduration(zx::handle handle, zx_duration_t data);");

    static_assert(internal::has_api_protocol_koid<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apikoid(zx::handle handle, zx_koid_t data);");

    static_assert(internal::has_api_protocol_paddr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apipaddr(zx::handle handle, zx_paddr_t data);");

    static_assert(internal::has_api_protocol_signals<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apisignals(zx::handle handle, zx_signals_t data);");

    static_assert(internal::has_api_protocol_time<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apitime(zx::handle handle, zx_time_t data);");

    static_assert(internal::has_api_protocol_vaddr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apivaddr(zx::handle handle, zx_vaddr_t data);");

    static_assert(internal::has_api_protocol_output_bool<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_bool(zx::handle handle, bool* out_result);");

    static_assert(internal::has_api_protocol_output_int8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_int8(zx::handle handle, int8_t* out_result);");

    static_assert(internal::has_api_protocol_output_int16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_int16(zx::handle handle, int16_t* out_result);");

    static_assert(internal::has_api_protocol_output_int32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_int32(zx::handle handle, int32_t* out_result);");

    static_assert(internal::has_api_protocol_output_int64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_int64(zx::handle handle, int64_t* out_result);");

    static_assert(internal::has_api_protocol_output_uint8<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_uint8(zx::handle handle, uint8_t* out_result);");

    static_assert(internal::has_api_protocol_output_uint16<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_uint16(zx::handle handle, uint16_t* out_result);");

    static_assert(internal::has_api_protocol_output_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_uint32(zx::handle handle, uint32_t* out_result);");

    static_assert(internal::has_api_protocol_output_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_uint64(zx::handle handle, uint64_t* out_result);");

    static_assert(internal::has_api_protocol_output_float32<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_float32(zx::handle handle, float* out_result);");

    static_assert(internal::has_api_protocol_output_float64<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_float64(zx::handle handle, double* out_result);");

    static_assert(internal::has_api_protocol_output_duration<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_duration(zx::handle handle, zx_duration_t* out_result);");

    static_assert(internal::has_api_protocol_output_koid<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_koid(zx::handle handle, zx_koid_t* out_result);");

    static_assert(internal::has_api_protocol_output_paddr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_paddr(zx::handle handle, zx_paddr_t* out_result);");

    static_assert(internal::has_api_protocol_output_signals<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_signals(zx::handle handle, zx_signals_t* out_result);");

    static_assert(internal::has_api_protocol_output_time<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_time(zx::handle handle, zx_time_t* out_result);");

    static_assert(internal::has_api_protocol_output_vaddr<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apioutput_vaddr(zx::handle handle, zx_vaddr_t* out_result);");

    static_assert(internal::has_api_protocol_return_void<D>::value,
        "ApiProtocol subclasses must implement "
        "void Apireturn_void(zx::handle handle);");

    static_assert(internal::has_api_protocol_return_status<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_status_t Apireturn_status(zx::handle handle);");

    static_assert(internal::has_api_protocol_return_ticks<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_ticks_t Apireturn_ticks(zx::handle handle);");

    static_assert(internal::has_api_protocol_return_time<D>::value,
        "ApiProtocol subclasses must implement "
        "zx_time_t Apireturn_time(zx::handle handle);");

    static_assert(internal::has_api_protocol_return_uint32<D>::value,
        "ApiProtocol subclasses must implement "
        "uint32_t Apireturn_uint32(zx::handle handle);");

    static_assert(internal::has_api_protocol_return_uint64<D>::value,
        "ApiProtocol subclasses must implement "
        "uint64_t Apireturn_uint64(zx::handle handle);");

}


} // namespace internal
} // namespace ddk
