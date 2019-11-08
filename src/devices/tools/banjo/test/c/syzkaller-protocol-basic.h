// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.basic banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct api_protocol api_protocol_t;

// Declarations
typedef struct api_protocol_ops {
    size_t (*usize)(void* ctx, size_t sz);
    bool (*bool)(void* ctx, bool b);
    int8_t (*int8)(void* ctx, int8_t i8);
    int16_t (*int16)(void* ctx, int16_t i16);
    int32_t (*int32)(void* ctx, int32_t i32);
    int64_t (*int64)(void* ctx, int64_t i64);
    uint8_t (*uint8)(void* ctx, uint8_t u8);
    uint16_t (*uint16)(void* ctx, uint16_t u16);
    uint32_t (*uint32)(void* ctx, uint32_t u32);
    uint64_t (*uint64)(void* ctx, uint64_t u64);
    void (*handle)(void* ctx, zx_handle_t h);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline size_t api_usize(const api_protocol_t* proto, size_t sz) {
    return proto->ops->usize(proto->ctx, sz);
}

static inline bool api_bool(const api_protocol_t* proto, bool b) {
    return proto->ops->bool(proto->ctx, b);
}

static inline int8_t api_int8(const api_protocol_t* proto, int8_t i8) {
    return proto->ops->int8(proto->ctx, i8);
}

static inline int16_t api_int16(const api_protocol_t* proto, int16_t i16) {
    return proto->ops->int16(proto->ctx, i16);
}

static inline int32_t api_int32(const api_protocol_t* proto, int32_t i32) {
    return proto->ops->int32(proto->ctx, i32);
}

static inline int64_t api_int64(const api_protocol_t* proto, int64_t i64) {
    return proto->ops->int64(proto->ctx, i64);
}

static inline uint8_t api_uint8(const api_protocol_t* proto, uint8_t u8) {
    return proto->ops->uint8(proto->ctx, u8);
}

static inline uint16_t api_uint16(const api_protocol_t* proto, uint16_t u16) {
    return proto->ops->uint16(proto->ctx, u16);
}

static inline uint32_t api_uint32(const api_protocol_t* proto, uint32_t u32) {
    return proto->ops->uint32(proto->ctx, u32);
}

static inline uint64_t api_uint64(const api_protocol_t* proto, uint64_t u64) {
    return proto->ops->uint64(proto->ctx, u64);
}

static inline void api_handle(const api_protocol_t* proto, zx_handle_t h) {
    proto->ops->handle(proto->ctx, h);
}



__END_CDECLS
