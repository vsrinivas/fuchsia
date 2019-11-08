// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.union banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef union packet packet_t;
typedef union primitive_types primitive_types_t;
typedef union arrays arrays_t;
typedef struct api_protocol api_protocol_t;

// Declarations
union packet {
    uint32_t i32;
    uint32_t u32;
};

union primitive_types {
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

union arrays {
    void rd_vptr[1];
    void wr_vptr[1];
    size_t rd_sz[1];
    size_t rd_sz[1];
    bool rd_b[1];
    bool wr_b[1];
    int8_t rd_i8[1];
    int8_t wr_i8[1];
    int16_t rd_i16[1];
    int16_t wr_i16[1];
    int32_t rd_i32[1];
    int32_t wr_i32[1];
    int64_t rd_i64[1];
    int64_t wr_i64[1];
    uint8_t rd_u8[1];
    uint8_t wr_u8[1];
    uint16_t rd_u16[1];
    uint16_t wr_u16[1];
    uint32_t rd_u32[1];
    uint32_t wr_u32[1];
    uint64_t rd_u64[1];
    uint64_t wr_u64[1];
    zx_handle_t rd_h[1];
    zx_handle_t wr_h[1];
};

typedef struct api_protocol_ops {
    zx_status_t (*packet)(void* ctx, zx_handle_t h, const packet_t* pkt);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t api_packet(const api_protocol_t* proto, zx_handle_t h, const packet_t* pkt) {
    return proto->ops->packet(proto->ctx, h, pkt);
}



__END_CDECLS
