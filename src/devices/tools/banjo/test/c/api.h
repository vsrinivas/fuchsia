// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.api banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct api_protocol api_protocol_t;
typedef struct api_protocol_ops api_protocol_ops_t;

// Declarations
struct api_protocol_ops {
    zx_status_t (*bool)(void* ctx, zx_handle_t handle, bool data);
    zx_status_t (*int8)(void* ctx, zx_handle_t handle, int8_t data);
    zx_status_t (*int16)(void* ctx, zx_handle_t handle, int16_t data);
    zx_status_t (*int32)(void* ctx, zx_handle_t handle, int32_t data);
    zx_status_t (*int64)(void* ctx, zx_handle_t handle, int64_t data);
    zx_status_t (*uint8)(void* ctx, zx_handle_t handle, uint8_t data);
    zx_status_t (*uint16)(void* ctx, zx_handle_t handle, uint16_t data);
    zx_status_t (*uint32)(void* ctx, zx_handle_t handle, uint32_t data);
    zx_status_t (*uint64)(void* ctx, zx_handle_t handle, uint64_t data);
    zx_status_t (*float32)(void* ctx, zx_handle_t handle, float data);
    zx_status_t (*float64)(void* ctx, zx_handle_t handle, double data);
    zx_status_t (*duration)(void* ctx, zx_handle_t handle, zx_duration_t data);
    zx_status_t (*koid)(void* ctx, zx_handle_t handle, zx_koid_t data);
    zx_status_t (*paddr)(void* ctx, zx_handle_t handle, zx_paddr_t data);
    zx_status_t (*signals)(void* ctx, zx_handle_t handle, zx_signals_t data);
    zx_status_t (*time)(void* ctx, zx_handle_t handle, zx_time_t data);
    zx_status_t (*vaddr)(void* ctx, zx_handle_t handle, zx_vaddr_t data);
    zx_status_t (*buffer)(void* ctx, zx_handle_t handle, const void data[size], uint32_t size);
    zx_status_t (*output_bool)(void* ctx, zx_handle_t handle, bool* out_result);
    zx_status_t (*output_int8)(void* ctx, zx_handle_t handle, int8_t* out_result);
    zx_status_t (*output_int16)(void* ctx, zx_handle_t handle, int16_t* out_result);
    zx_status_t (*output_int32)(void* ctx, zx_handle_t handle, int32_t* out_result);
    zx_status_t (*output_int64)(void* ctx, zx_handle_t handle, int64_t* out_result);
    zx_status_t (*output_uint8)(void* ctx, zx_handle_t handle, uint8_t* out_result);
    zx_status_t (*output_uint16)(void* ctx, zx_handle_t handle, uint16_t* out_result);
    zx_status_t (*output_uint32)(void* ctx, zx_handle_t handle, uint32_t* out_result);
    zx_status_t (*output_uint64)(void* ctx, zx_handle_t handle, uint64_t* out_result);
    zx_status_t (*output_float32)(void* ctx, zx_handle_t handle, float* out_result);
    zx_status_t (*output_float64)(void* ctx, zx_handle_t handle, double* out_result);
    zx_status_t (*output_duration)(void* ctx, zx_handle_t handle, zx_duration_t* out_result);
    zx_status_t (*output_koid)(void* ctx, zx_handle_t handle, zx_koid_t* out_result);
    zx_status_t (*output_paddr)(void* ctx, zx_handle_t handle, zx_paddr_t* out_result);
    zx_status_t (*output_signals)(void* ctx, zx_handle_t handle, zx_signals_t* out_result);
    zx_status_t (*output_time)(void* ctx, zx_handle_t handle, zx_time_t* out_result);
    zx_status_t (*output_vaddr)(void* ctx, zx_handle_t handle, zx_vaddr_t* out_result);
    zx_status_t (*output_buffer)(void* ctx, zx_handle_t handle, const void data[size], uint32_t size, uint32_t* out_actual);
    void (*return_void)(void* ctx, zx_handle_t handle);
    zx_status_t (*return_status)(void* ctx, zx_handle_t handle);
    zx_ticks_t (*return_ticks)(void* ctx, zx_handle_t handle);
    zx_time_t (*return_time)(void* ctx, zx_handle_t handle);
    uint32_t (*return_uint32)(void* ctx, zx_handle_t handle);
    uint64_t (*return_uint64)(void* ctx, zx_handle_t handle);
};


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
static inline zx_status_t api_bool(const api_protocol_t* proto, zx_handle_t handle, bool data) {
    return proto->ops->bool(proto->ctx, handle, data);
}

static inline zx_status_t api_int8(const api_protocol_t* proto, zx_handle_t handle, int8_t data) {
    return proto->ops->int8(proto->ctx, handle, data);
}

static inline zx_status_t api_int16(const api_protocol_t* proto, zx_handle_t handle, int16_t data) {
    return proto->ops->int16(proto->ctx, handle, data);
}

static inline zx_status_t api_int32(const api_protocol_t* proto, zx_handle_t handle, int32_t data) {
    return proto->ops->int32(proto->ctx, handle, data);
}

static inline zx_status_t api_int64(const api_protocol_t* proto, zx_handle_t handle, int64_t data) {
    return proto->ops->int64(proto->ctx, handle, data);
}

static inline zx_status_t api_uint8(const api_protocol_t* proto, zx_handle_t handle, uint8_t data) {
    return proto->ops->uint8(proto->ctx, handle, data);
}

static inline zx_status_t api_uint16(const api_protocol_t* proto, zx_handle_t handle, uint16_t data) {
    return proto->ops->uint16(proto->ctx, handle, data);
}

static inline zx_status_t api_uint32(const api_protocol_t* proto, zx_handle_t handle, uint32_t data) {
    return proto->ops->uint32(proto->ctx, handle, data);
}

static inline zx_status_t api_uint64(const api_protocol_t* proto, zx_handle_t handle, uint64_t data) {
    return proto->ops->uint64(proto->ctx, handle, data);
}

static inline zx_status_t api_float32(const api_protocol_t* proto, zx_handle_t handle, float data) {
    return proto->ops->float32(proto->ctx, handle, data);
}

static inline zx_status_t api_float64(const api_protocol_t* proto, zx_handle_t handle, double data) {
    return proto->ops->float64(proto->ctx, handle, data);
}

static inline zx_status_t api_duration(const api_protocol_t* proto, zx_handle_t handle, zx_duration_t data) {
    return proto->ops->duration(proto->ctx, handle, data);
}

static inline zx_status_t api_koid(const api_protocol_t* proto, zx_handle_t handle, zx_koid_t data) {
    return proto->ops->koid(proto->ctx, handle, data);
}

static inline zx_status_t api_paddr(const api_protocol_t* proto, zx_handle_t handle, zx_paddr_t data) {
    return proto->ops->paddr(proto->ctx, handle, data);
}

static inline zx_status_t api_signals(const api_protocol_t* proto, zx_handle_t handle, zx_signals_t data) {
    return proto->ops->signals(proto->ctx, handle, data);
}

static inline zx_status_t api_time(const api_protocol_t* proto, zx_handle_t handle, zx_time_t data) {
    return proto->ops->time(proto->ctx, handle, data);
}

static inline zx_status_t api_vaddr(const api_protocol_t* proto, zx_handle_t handle, zx_vaddr_t data) {
    return proto->ops->vaddr(proto->ctx, handle, data);
}

static inline zx_status_t api_buffer(const api_protocol_t* proto, zx_handle_t handle, const void data[size], uint32_t size) {
    return proto->ops->buffer(proto->ctx, handle, data, size);
}

static inline zx_status_t api_output_bool(const api_protocol_t* proto, zx_handle_t handle, bool* out_result) {
    return proto->ops->output_bool(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_int8(const api_protocol_t* proto, zx_handle_t handle, int8_t* out_result) {
    return proto->ops->output_int8(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_int16(const api_protocol_t* proto, zx_handle_t handle, int16_t* out_result) {
    return proto->ops->output_int16(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_int32(const api_protocol_t* proto, zx_handle_t handle, int32_t* out_result) {
    return proto->ops->output_int32(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_int64(const api_protocol_t* proto, zx_handle_t handle, int64_t* out_result) {
    return proto->ops->output_int64(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_uint8(const api_protocol_t* proto, zx_handle_t handle, uint8_t* out_result) {
    return proto->ops->output_uint8(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_uint16(const api_protocol_t* proto, zx_handle_t handle, uint16_t* out_result) {
    return proto->ops->output_uint16(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_uint32(const api_protocol_t* proto, zx_handle_t handle, uint32_t* out_result) {
    return proto->ops->output_uint32(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_uint64(const api_protocol_t* proto, zx_handle_t handle, uint64_t* out_result) {
    return proto->ops->output_uint64(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_float32(const api_protocol_t* proto, zx_handle_t handle, float* out_result) {
    return proto->ops->output_float32(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_float64(const api_protocol_t* proto, zx_handle_t handle, double* out_result) {
    return proto->ops->output_float64(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_duration(const api_protocol_t* proto, zx_handle_t handle, zx_duration_t* out_result) {
    return proto->ops->output_duration(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_koid(const api_protocol_t* proto, zx_handle_t handle, zx_koid_t* out_result) {
    return proto->ops->output_koid(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_paddr(const api_protocol_t* proto, zx_handle_t handle, zx_paddr_t* out_result) {
    return proto->ops->output_paddr(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_signals(const api_protocol_t* proto, zx_handle_t handle, zx_signals_t* out_result) {
    return proto->ops->output_signals(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_time(const api_protocol_t* proto, zx_handle_t handle, zx_time_t* out_result) {
    return proto->ops->output_time(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_vaddr(const api_protocol_t* proto, zx_handle_t handle, zx_vaddr_t* out_result) {
    return proto->ops->output_vaddr(proto->ctx, handle, out_result);
}

static inline zx_status_t api_output_buffer(const api_protocol_t* proto, zx_handle_t handle, const void data[size], uint32_t size, uint32_t* out_actual) {
    return proto->ops->output_buffer(proto->ctx, handle, data, size, out_actual);
}

static inline void api_return_void(const api_protocol_t* proto, zx_handle_t handle) {
    proto->ops->return_void(proto->ctx, handle);
}

static inline zx_status_t api_return_status(const api_protocol_t* proto, zx_handle_t handle) {
    return proto->ops->return_status(proto->ctx, handle);
}

static inline zx_ticks_t api_return_ticks(const api_protocol_t* proto, zx_handle_t handle) {
    return proto->ops->return_ticks(proto->ctx, handle);
}

static inline zx_time_t api_return_time(const api_protocol_t* proto, zx_handle_t handle) {
    return proto->ops->return_time(proto->ctx, handle);
}

static inline uint32_t api_return_uint32(const api_protocol_t* proto, zx_handle_t handle) {
    return proto->ops->return_uint32(proto->ctx, handle);
}

static inline uint64_t api_return_uint64(const api_protocol_t* proto, zx_handle_t handle) {
    return proto->ops->return_uint64(proto->ctx, handle);
}


__END_CDECLS
