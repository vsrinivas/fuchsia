// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example9 banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct echo_more echo_more_t;
typedef uint32_t echo_me_t;
#define ECHO_ME_ZERO UINT32_C(0)
#define ECHO_ME_ONE UINT32_C(1)
#define favorite_echo UINT32_C(0)
typedef struct echo_protocol echo_protocol_t;
typedef struct echo_protocol_ops echo_protocol_ops_t;

// Declarations
struct echo_more {
    uint32_t first;
    uint64_t second;
};

struct echo_protocol_ops {
    uint32_t (*echo32)(void* ctx, uint32_t uint32);
    uint64_t (*echo64)(void* ctx, uint64_t uint64);
    echo_me_t (*echo_enum)(void* ctx, echo_me_t req);
    void (*echo_handle)(void* ctx, zx_handle_t req, zx_handle_t* out_response);
    void (*echo_channel)(void* ctx, zx_handle_t req, zx_handle_t* out_response);
    void (*echo_struct)(void* ctx, const echo_more_t* req, echo_more_t* out_response);
};


struct echo_protocol {
    const echo_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
static inline uint32_t echo_echo32(const echo_protocol_t* proto, uint32_t uint32) {
    return proto->ops->echo32(proto->ctx, uint32);
}

static inline uint64_t echo_echo64(const echo_protocol_t* proto, uint64_t uint64) {
    return proto->ops->echo64(proto->ctx, uint64);
}

static inline echo_me_t echo_echo_enum(const echo_protocol_t* proto, echo_me_t req) {
    return proto->ops->echo_enum(proto->ctx, req);
}

static inline void echo_echo_handle(const echo_protocol_t* proto, zx_handle_t req, zx_handle_t* out_response) {
    proto->ops->echo_handle(proto->ctx, req, out_response);
}

static inline void echo_echo_channel(const echo_protocol_t* proto, zx_handle_t req, zx_handle_t* out_response) {
    proto->ops->echo_channel(proto->ctx, req, out_response);
}

static inline void echo_echo_struct(const echo_protocol_t* proto, const echo_more_t* req, echo_more_t* out_response) {
    proto->ops->echo_struct(proto->ctx, req, out_response);
}


__END_CDECLS
