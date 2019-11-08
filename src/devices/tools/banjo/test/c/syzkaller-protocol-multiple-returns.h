// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.multiple.returns banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct api_protocol api_protocol_t;

// Declarations
typedef struct api_protocol_ops {
    zx_status_t (*usize)(void* ctx, size_t sz, size_t* out_sz_1);
    zx_status_t (*bool)(void* ctx, bool b, bool* out_b_1);
    zx_status_t (*int8)(void* ctx, int8_t i8, int8_t* out_i8_1);
    zx_status_t (*int16)(void* ctx, int16_t i16, int16_t* out_i16_1);
    zx_status_t (*int32)(void* ctx, int32_t i32, int32_t* out_i32_1);
    zx_status_t (*int64)(void* ctx, int64_t i64, int64_t* out_i64_1);
    zx_status_t (*uint8)(void* ctx, uint8_t u8, uint8_t* out_u8_1);
    zx_status_t (*uint16)(void* ctx, uint16_t u16, uint16_t* out_u16_1);
    zx_status_t (*uint32)(void* ctx, uint32_t u32, uint32_t* out_u32_1);
    zx_status_t (*uint64)(void* ctx, uint64_t u64, uint64_t* out_u64_1);
    zx_status_t (*handle)(void* ctx, zx_handle_t h, zx_handle_t* out_h_1);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t api_usize(const api_protocol_t* proto, size_t sz, size_t* out_sz_1) {
    return proto->ops->usize(proto->ctx, sz, out_sz_1);
}

static inline zx_status_t api_bool(const api_protocol_t* proto, bool b, bool* out_b_1) {
    return proto->ops->bool(proto->ctx, b, out_b_1);
}

static inline zx_status_t api_int8(const api_protocol_t* proto, int8_t i8, int8_t* out_i8_1) {
    return proto->ops->int8(proto->ctx, i8, out_i8_1);
}

static inline zx_status_t api_int16(const api_protocol_t* proto, int16_t i16, int16_t* out_i16_1) {
    return proto->ops->int16(proto->ctx, i16, out_i16_1);
}

static inline zx_status_t api_int32(const api_protocol_t* proto, int32_t i32, int32_t* out_i32_1) {
    return proto->ops->int32(proto->ctx, i32, out_i32_1);
}

static inline zx_status_t api_int64(const api_protocol_t* proto, int64_t i64, int64_t* out_i64_1) {
    return proto->ops->int64(proto->ctx, i64, out_i64_1);
}

static inline zx_status_t api_uint8(const api_protocol_t* proto, uint8_t u8, uint8_t* out_u8_1) {
    return proto->ops->uint8(proto->ctx, u8, out_u8_1);
}

static inline zx_status_t api_uint16(const api_protocol_t* proto, uint16_t u16, uint16_t* out_u16_1) {
    return proto->ops->uint16(proto->ctx, u16, out_u16_1);
}

static inline zx_status_t api_uint32(const api_protocol_t* proto, uint32_t u32, uint32_t* out_u32_1) {
    return proto->ops->uint32(proto->ctx, u32, out_u32_1);
}

static inline zx_status_t api_uint64(const api_protocol_t* proto, uint64_t u64, uint64_t* out_u64_1) {
    return proto->ops->uint64(proto->ctx, u64, out_u64_1);
}

static inline zx_status_t api_handle(const api_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h_1) {
    return proto->ops->handle(proto->ctx, h, out_h_1);
}



__END_CDECLS
