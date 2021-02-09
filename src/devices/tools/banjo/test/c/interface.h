// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.interface banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct baker_protocol baker_protocol_t;
typedef struct baker_protocol_ops baker_protocol_ops_t;
typedef uint32_t cookie_kind_t;
#define COOKIE_KIND_CHOCOLATE UINT32_C(0)
#define COOKIE_KIND_GINGERBREAD UINT32_C(1)
#define COOKIE_KIND_SNICKERDOODLE UINT32_C(2)
typedef void (*cookie_maker_prep_callback)(void* ctx, uint64_t token);
typedef void (*cookie_maker_bake_callback)(void* ctx, zx_status_t s);
typedef struct cookie_maker_protocol cookie_maker_protocol_t;
typedef struct cookie_maker_protocol_ops cookie_maker_protocol_ops_t;

// Declarations
struct baker_protocol_ops {
    void (*register)(void* ctx, const cookie_maker_protocol_t* intf);
    void (*de_register)(void* ctx);
};


struct baker_protocol {
    baker_protocol_ops_t* ops;
    void* ctx;
};

struct cookie_maker_protocol_ops {
    void (*prep)(void* ctx, cookie_kind_t cookie, cookie_maker_prep_callback callback, void* cookie);
    void (*bake)(void* ctx, uint64_t token, zx_time_t time, cookie_maker_bake_callback callback, void* cookie);
    zx_status_t (*deliver)(void* ctx, uint64_t token);
};


struct cookie_maker_protocol {
    cookie_maker_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
// Registers a cookie maker device which the baker can use.
static inline void baker_register(const baker_protocol_t* proto, void* intf_ctx, cookie_maker_protocol_ops_t* intf_ops) {
    const cookie_maker_protocol_t intf2 = {
        .ops = intf_ops,
        .ctx = intf_ctx,
    };
    const cookie_maker_protocol_t* intf = &intf2;
    proto->ops->register(proto->ctx, intf);
}

// De-registers a cookie maker device when it's no longer available.
static inline void baker_de_register(const baker_protocol_t* proto) {
    proto->ops->de_register(proto->ctx);
}

// Asynchonously preps a cookie.
static inline void cookie_maker_prep(const cookie_maker_protocol_t* proto, cookie_kind_t cookie, cookie_maker_prep_callback callback, void* cookie) {
    proto->ops->prep(proto->ctx, cookie, callback, cookie);
}

// Asynchonously bakes a cookie.
// Must only be called after preping finishes.
static inline void cookie_maker_bake(const cookie_maker_protocol_t* proto, uint64_t token, zx_time_t time, cookie_maker_bake_callback callback, void* cookie) {
    proto->ops->bake(proto->ctx, token, time, callback, cookie);
}

// Synchronously deliver a cookie.
// Must be called only after Bake finishes.
static inline zx_status_t cookie_maker_deliver(const cookie_maker_protocol_t* proto, uint64_t token) {
    return proto->ops->deliver(proto->ctx, token);
}


__END_CDECLS
