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
typedef uint32_t cookie_kind_t;
#define COOKIE_KIND_CHOCOLATE UINT32_C(0)
#define COOKIE_KIND_GINGERBREAD UINT32_C(1)
#define COOKIE_KIND_SNICKERDOODLE UINT32_C(2)
typedef void (*cookie_maker_prep_callback)(void* ctx, uint64_t token);
typedef void (*cookie_maker_bake_callback)(void* ctx, zx_status_t s);
typedef struct cookie_maker_protocol cookie_maker_protocol_t;
typedef struct cookie_maker_protocol_ops cookie_maker_protocol_ops_t;
typedef struct cookie_jar_args cookie_jar_args_t;
typedef struct cookie_jarrer_protocol cookie_jarrer_protocol_t;
typedef struct cookie_jarrer_protocol_ops cookie_jarrer_protocol_ops_t;
typedef union change_args change_args_t;
typedef struct baker_protocol baker_protocol_t;
typedef struct baker_protocol_ops baker_protocol_ops_t;

// Declarations
struct cookie_maker_protocol_ops {
    void (*prep)(void* ctx, cookie_kind_t cookie, cookie_maker_prep_callback callback, void* cookie);
    void (*bake)(void* ctx, uint64_t token, zx_time_t time, cookie_maker_bake_callback callback, void* cookie);
    zx_status_t (*deliver)(void* ctx, uint64_t token);
};


struct cookie_maker_protocol {
    cookie_maker_protocol_ops_t* ops;
    void* ctx;
};

// To do things to a cookie jar, we need to know which jar we are doing them to.
struct cookie_jar_args {
    // To whom does this jar belong?
    char name[100];
};

struct cookie_jarrer_protocol_ops {
    void (*place)(void* ctx, const char* name);
    cookie_kind_t (*take)(void* ctx, const char* name);
};


struct cookie_jarrer_protocol {
    cookie_jarrer_protocol_ops_t* ops;
    void* ctx;
};

// Swap devices at the bakery, changing either the maker OR the jarrer out.
union change_args {
    cookie_maker_protocol_t intf;
    cookie_jarrer_protocol_t jarrer;
};

struct baker_protocol_ops {
    void (*register)(void* ctx, const cookie_maker_protocol_t* intf, const cookie_jarrer_protocol_t* jar);
    void (*change)(void* ctx, const change_args_t* payload, change_args_t* out_payload);
    void (*de_register)(void* ctx);
};


struct baker_protocol {
    baker_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
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

// Place a cookie in the named jar. If no jar with the supplied name exists, one is created.
static inline void cookie_jarrer_place(const cookie_jarrer_protocol_t* proto, const char* name) {
    proto->ops->place(proto->ctx, name);
}

// Who took a cookie from the cookie jar?
static inline cookie_kind_t cookie_jarrer_take(const cookie_jarrer_protocol_t* proto, const char* name) {
    return proto->ops->take(proto->ctx, name);
}

// Registers a cookie maker device which the baker can use, and a cookie jar into
// which they can place their completed cookies.
static inline void baker_register(const baker_protocol_t* proto, void* intf_ctx, cookie_maker_protocol_ops_t* intf_ops, void* jar_ctx, cookie_jarrer_protocol_ops_t* jar_ops) {
    const cookie_maker_protocol_t intf2 = {
        .ops = intf_ops,
        .ctx = intf_ctx,
    };
    const cookie_maker_protocol_t* intf = &intf2;
    const cookie_jarrer_protocol_t jar2 = {
        .ops = jar_ops,
        .ctx = jar_ctx,
    };
    const cookie_jarrer_protocol_t* jar = &jar2;
    proto->ops->register(proto->ctx, intf, jar);
}

// Swap out the maker or jarrer for a different one.
static inline void baker_change(const baker_protocol_t* proto, const change_args_t* payload, change_args_t* out_payload) {
    proto->ops->change(proto->ctx, payload, out_payload);
}

// De-registers a cookie maker device when it's no longer available.
static inline void baker_de_register(const baker_protocol_t* proto) {
    proto->ops->de_register(proto->ctx);
}


__END_CDECLS
