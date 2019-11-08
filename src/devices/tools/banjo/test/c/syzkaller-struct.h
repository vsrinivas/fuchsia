// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.struct banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct point point_t;
typedef struct primitive_types primitive_types_t;
typedef struct strings strings_t;
typedef struct arrays arrays_t;
typedef struct api_protocol api_protocol_t;

// Declarations
struct point {
    int32_t x;
    int32_t y;
};

struct primitive_types {
    bool b;
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    zx_handle_t h;
};

struct strings {
    char rd_str[rd_str_len];
    char wr_str[wr_str_len];
    size_t rd_str_len;
    size_t wr_str_len;
};

struct arrays {
    void rd_vptr[rd_vptr_len];
    void wr_vptr[wr_vptr_len];
    size_t rd_vptr_len;
    size_t wr_vptr_len;
    size_t rd_sz[rd_sz_len];
    size_t rd_sz[wr_sz_len];
    size_t rd_sz_len;
    size_t wr_sz_len;
    bool rd_b[rd_b_len];
    bool wr_b[wr_b_len];
    size_t rd_b_len;
    size_t wr_b_len;
    int8_t rd_i8[rd_i8_len];
    int8_t wr_i8[wr_i8_len];
    size_t rd_i8_len;
    size_t wr_i8_len;
    int16_t rd_i16[rd_i16_len];
    int16_t wr_i16[wr_i16_len];
    size_t rd_i16_len;
    size_t wr_i16_len;
    int32_t rd_i32[rd_i32_len];
    int32_t wr_i32[wr_i32_len];
    size_t rd_i32_len;
    size_t wr_i32_len;
    int64_t rd_i64[rd_i64_len];
    int64_t wr_i64[wr_i64_len];
    size_t rd_i64_len;
    size_t wr_i64_len;
    uint8_t rd_u8[rd_u8_len];
    uint8_t wr_u8[wr_u8_len];
    size_t rd_u8_len;
    size_t wr_u8_len;
    uint16_t rd_u16[rd_u16_len];
    uint16_t wr_u16[wr_u16_len];
    size_t rd_u16_len;
    size_t wr_u16_len;
    uint32_t rd_u32[rd_u32_len];
    uint32_t wr_u32[wr_u32_len];
    size_t rd_u32_len;
    size_t wr_u32_len;
    uint64_t rd_u64[rd_u64_len];
    uint64_t wr_u64[wr_u64_len];
    size_t rd_u64_len;
    size_t wr_u64_len;
    zx_handle_t rd_h[rd_h_len];
    zx_handle_t wr_h[wr_h_len];
    size_t rd_h_len;
    size_t wr_h_len;
};

typedef struct api_protocol_ops {
    zx_status_t (*point)(void* ctx, zx_handle_t h, const point_t* pt);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t api_point(const api_protocol_t* proto, zx_handle_t h, const point_t* pt) {
    return proto->ops->point(proto->ctx, h, pt);
}



__END_CDECLS
