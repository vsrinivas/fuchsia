// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.array banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct api_protocol api_protocol_t;

// Declarations
typedef struct api_protocol_ops {
    zx_status_t (*void_ptr)(void* ctx, const void vptr[1]);
    zx_status_t (*void_ptr)(void* ctx, const void vptr[1]);
    zx_status_t (*void_ptr)(void* ctx, const void vptr[vptr_len], size_t vptr_len);
    zx_status_t (*void_ptr)(void* ctx, const void vptr[vptr_len], size_t vptr_len);
    zx_status_t (*usize)(void* ctx, const size_t sz[1]);
    zx_status_t (*usize)(void* ctx, const size_t sz[1]);
    zx_status_t (*usize)(void* ctx, const size_t sz[sz_len], size_t sz_len);
    zx_status_t (*usize)(void* ctx, const size_t sz[sz_len], size_t sz_len);
    zx_status_t (*bool)(void* ctx, const bool b[1]);
    zx_status_t (*bool)(void* ctx, const bool b[1]);
    zx_status_t (*bool)(void* ctx, const bool b[b_len], size_t b_len);
    zx_status_t (*bool)(void* ctx, const bool b[b_len], size_t b_len);
    zx_status_t (*int8)(void* ctx, const int8_t i8[1]);
    zx_status_t (*int8)(void* ctx, const int8_t i8[1]);
    zx_status_t (*int8)(void* ctx, const int8_t i8[i8_len], size_t i8_len);
    zx_status_t (*int8)(void* ctx, const int8_t i8[i8_len], size_t i8_len);
    zx_status_t (*int16)(void* ctx, const int16_t i16[1]);
    zx_status_t (*int16)(void* ctx, const int16_t i16[1]);
    zx_status_t (*int16)(void* ctx, const int16_t i16[i16_len], size_t i16_len);
    zx_status_t (*int16)(void* ctx, const int16_t i16[i16_len], size_t i16_len);
    zx_status_t (*int32)(void* ctx, const int32_t i32[1]);
    zx_status_t (*int32)(void* ctx, const int32_t i32[1]);
    zx_status_t (*int32)(void* ctx, const int32_t i32[i32_len], size_t i32_len);
    zx_status_t (*int32)(void* ctx, const int32_t i32[i32_len], size_t i32_len);
    zx_status_t (*int64)(void* ctx, const int64_t i64[1]);
    zx_status_t (*int64)(void* ctx, const int64_t i64[1]);
    zx_status_t (*int64)(void* ctx, const int64_t i64[i64_len], size_t i64_len);
    zx_status_t (*int64)(void* ctx, const int64_t i64[i64_len], size_t i64_len);
    zx_status_t (*uint8)(void* ctx, const uint8_t u8[1]);
    zx_status_t (*uint8)(void* ctx, const uint8_t u8[1]);
    zx_status_t (*uint8)(void* ctx, const uint8_t u8[u8_len], size_t u8_len);
    zx_status_t (*uint8)(void* ctx, const uint8_t u8[u8_len], size_t u8_len);
    zx_status_t (*uint16)(void* ctx, const uint16_t u16[1]);
    zx_status_t (*uint16)(void* ctx, const uint16_t u16[1]);
    zx_status_t (*uint16)(void* ctx, const uint16_t u16[u16_len], size_t u16_len);
    zx_status_t (*uint16)(void* ctx, const uint16_t u16[u16_len], size_t u16_len);
    zx_status_t (*uint32)(void* ctx, const uint32_t u32[1]);
    zx_status_t (*uint32)(void* ctx, const uint32_t u32[1]);
    zx_status_t (*uint32)(void* ctx, const uint32_t u32[u32_len], size_t u32_len);
    zx_status_t (*uint32)(void* ctx, const uint32_t u32[u32_len], size_t u32_len);
    zx_status_t (*uint64)(void* ctx, const uint64_t u64[1]);
    zx_status_t (*uint64)(void* ctx, const uint64_t u64[1]);
    zx_status_t (*uint64)(void* ctx, const uint64_t u64[u64_len], size_t u64_len);
    zx_status_t (*uint64)(void* ctx, const uint64_t u64[u64_len], size_t u64_len);
    zx_status_t (*handle)(void* ctx, const zx_handle_t h[1]);
    zx_status_t (*handle)(void* ctx, const zx_handle_t h[1]);
    zx_status_t (*handle)(void* ctx, const zx_handle_t h[h_len], size_t h_len);
    zx_status_t (*handle)(void* ctx, const zx_handle_t h[h_len], size_t h_len);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t api_void_ptr(const api_protocol_t* proto, const void vptr[1]) {
    return proto->ops->void_ptr(proto->ctx, vptr);
}

static inline zx_status_t api_void_ptr(const api_protocol_t* proto, const void vptr[1]) {
    return proto->ops->void_ptr(proto->ctx, vptr);
}

static inline zx_status_t api_void_ptr(const api_protocol_t* proto, const void vptr[vptr_len], size_t vptr_len) {
    return proto->ops->void_ptr(proto->ctx, vptr, vptr_len);
}

static inline zx_status_t api_void_ptr(const api_protocol_t* proto, const void vptr[vptr_len], size_t vptr_len) {
    return proto->ops->void_ptr(proto->ctx, vptr, vptr_len);
}

static inline zx_status_t api_usize(const api_protocol_t* proto, const size_t sz[1]) {
    return proto->ops->usize(proto->ctx, sz);
}

static inline zx_status_t api_usize(const api_protocol_t* proto, const size_t sz[1]) {
    return proto->ops->usize(proto->ctx, sz);
}

static inline zx_status_t api_usize(const api_protocol_t* proto, const size_t sz[sz_len], size_t sz_len) {
    return proto->ops->usize(proto->ctx, sz, sz_len);
}

static inline zx_status_t api_usize(const api_protocol_t* proto, const size_t sz[sz_len], size_t sz_len) {
    return proto->ops->usize(proto->ctx, sz, sz_len);
}

static inline zx_status_t api_bool(const api_protocol_t* proto, const bool b[1]) {
    return proto->ops->bool(proto->ctx, b);
}

static inline zx_status_t api_bool(const api_protocol_t* proto, const bool b[1]) {
    return proto->ops->bool(proto->ctx, b);
}

static inline zx_status_t api_bool(const api_protocol_t* proto, const bool b[b_len], size_t b_len) {
    return proto->ops->bool(proto->ctx, b, b_len);
}

static inline zx_status_t api_bool(const api_protocol_t* proto, const bool b[b_len], size_t b_len) {
    return proto->ops->bool(proto->ctx, b, b_len);
}

static inline zx_status_t api_int8(const api_protocol_t* proto, const int8_t i8[1]) {
    return proto->ops->int8(proto->ctx, i8);
}

static inline zx_status_t api_int8(const api_protocol_t* proto, const int8_t i8[1]) {
    return proto->ops->int8(proto->ctx, i8);
}

static inline zx_status_t api_int8(const api_protocol_t* proto, const int8_t i8[i8_len], size_t i8_len) {
    return proto->ops->int8(proto->ctx, i8, i8_len);
}

static inline zx_status_t api_int8(const api_protocol_t* proto, const int8_t i8[i8_len], size_t i8_len) {
    return proto->ops->int8(proto->ctx, i8, i8_len);
}

static inline zx_status_t api_int16(const api_protocol_t* proto, const int16_t i16[1]) {
    return proto->ops->int16(proto->ctx, i16);
}

static inline zx_status_t api_int16(const api_protocol_t* proto, const int16_t i16[1]) {
    return proto->ops->int16(proto->ctx, i16);
}

static inline zx_status_t api_int16(const api_protocol_t* proto, const int16_t i16[i16_len], size_t i16_len) {
    return proto->ops->int16(proto->ctx, i16, i16_len);
}

static inline zx_status_t api_int16(const api_protocol_t* proto, const int16_t i16[i16_len], size_t i16_len) {
    return proto->ops->int16(proto->ctx, i16, i16_len);
}

static inline zx_status_t api_int32(const api_protocol_t* proto, const int32_t i32[1]) {
    return proto->ops->int32(proto->ctx, i32);
}

static inline zx_status_t api_int32(const api_protocol_t* proto, const int32_t i32[1]) {
    return proto->ops->int32(proto->ctx, i32);
}

static inline zx_status_t api_int32(const api_protocol_t* proto, const int32_t i32[i32_len], size_t i32_len) {
    return proto->ops->int32(proto->ctx, i32, i32_len);
}

static inline zx_status_t api_int32(const api_protocol_t* proto, const int32_t i32[i32_len], size_t i32_len) {
    return proto->ops->int32(proto->ctx, i32, i32_len);
}

static inline zx_status_t api_int64(const api_protocol_t* proto, const int64_t i64[1]) {
    return proto->ops->int64(proto->ctx, i64);
}

static inline zx_status_t api_int64(const api_protocol_t* proto, const int64_t i64[1]) {
    return proto->ops->int64(proto->ctx, i64);
}

static inline zx_status_t api_int64(const api_protocol_t* proto, const int64_t i64[i64_len], size_t i64_len) {
    return proto->ops->int64(proto->ctx, i64, i64_len);
}

static inline zx_status_t api_int64(const api_protocol_t* proto, const int64_t i64[i64_len], size_t i64_len) {
    return proto->ops->int64(proto->ctx, i64, i64_len);
}

static inline zx_status_t api_uint8(const api_protocol_t* proto, const uint8_t u8[1]) {
    return proto->ops->uint8(proto->ctx, u8);
}

static inline zx_status_t api_uint8(const api_protocol_t* proto, const uint8_t u8[1]) {
    return proto->ops->uint8(proto->ctx, u8);
}

static inline zx_status_t api_uint8(const api_protocol_t* proto, const uint8_t u8[u8_len], size_t u8_len) {
    return proto->ops->uint8(proto->ctx, u8, u8_len);
}

static inline zx_status_t api_uint8(const api_protocol_t* proto, const uint8_t u8[u8_len], size_t u8_len) {
    return proto->ops->uint8(proto->ctx, u8, u8_len);
}

static inline zx_status_t api_uint16(const api_protocol_t* proto, const uint16_t u16[1]) {
    return proto->ops->uint16(proto->ctx, u16);
}

static inline zx_status_t api_uint16(const api_protocol_t* proto, const uint16_t u16[1]) {
    return proto->ops->uint16(proto->ctx, u16);
}

static inline zx_status_t api_uint16(const api_protocol_t* proto, const uint16_t u16[u16_len], size_t u16_len) {
    return proto->ops->uint16(proto->ctx, u16, u16_len);
}

static inline zx_status_t api_uint16(const api_protocol_t* proto, const uint16_t u16[u16_len], size_t u16_len) {
    return proto->ops->uint16(proto->ctx, u16, u16_len);
}

static inline zx_status_t api_uint32(const api_protocol_t* proto, const uint32_t u32[1]) {
    return proto->ops->uint32(proto->ctx, u32);
}

static inline zx_status_t api_uint32(const api_protocol_t* proto, const uint32_t u32[1]) {
    return proto->ops->uint32(proto->ctx, u32);
}

static inline zx_status_t api_uint32(const api_protocol_t* proto, const uint32_t u32[u32_len], size_t u32_len) {
    return proto->ops->uint32(proto->ctx, u32, u32_len);
}

static inline zx_status_t api_uint32(const api_protocol_t* proto, const uint32_t u32[u32_len], size_t u32_len) {
    return proto->ops->uint32(proto->ctx, u32, u32_len);
}

static inline zx_status_t api_uint64(const api_protocol_t* proto, const uint64_t u64[1]) {
    return proto->ops->uint64(proto->ctx, u64);
}

static inline zx_status_t api_uint64(const api_protocol_t* proto, const uint64_t u64[1]) {
    return proto->ops->uint64(proto->ctx, u64);
}

static inline zx_status_t api_uint64(const api_protocol_t* proto, const uint64_t u64[u64_len], size_t u64_len) {
    return proto->ops->uint64(proto->ctx, u64, u64_len);
}

static inline zx_status_t api_uint64(const api_protocol_t* proto, const uint64_t u64[u64_len], size_t u64_len) {
    return proto->ops->uint64(proto->ctx, u64, u64_len);
}

static inline zx_status_t api_handle(const api_protocol_t* proto, const zx_handle_t h[1]) {
    return proto->ops->handle(proto->ctx, h);
}

static inline zx_status_t api_handle(const api_protocol_t* proto, const zx_handle_t h[1]) {
    return proto->ops->handle(proto->ctx, h);
}

static inline zx_status_t api_handle(const api_protocol_t* proto, const zx_handle_t h[h_len], size_t h_len) {
    return proto->ops->handle(proto->ctx, h, h_len);
}

static inline zx_status_t api_handle(const api_protocol_t* proto, const zx_handle_t h[h_len], size_t h_len) {
    return proto->ops->handle(proto->ctx, h, h_len);
}



__END_CDECLS
