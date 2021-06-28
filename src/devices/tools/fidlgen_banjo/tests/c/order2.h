// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.order2 banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct foo_protocol foo_protocol_t;
typedef struct foo_protocol_ops foo_protocol_ops_t;
typedef struct bar_protocol bar_protocol_t;
typedef struct bar_protocol_ops bar_protocol_ops_t;

// Declarations
struct foo_protocol_ops {
    void (*hello)(void* ctx);
};


struct foo_protocol {
    foo_protocol_ops_t* ops;
    void* ctx;
};

struct bar_protocol_ops {
    void (*world)(void* ctx, const foo_protocol_t* foo);
};


struct bar_protocol {
    bar_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
static inline void foo_hello(const foo_protocol_t* proto) {
    proto->ops->hello(proto->ctx);
}

static inline void bar_world(const bar_protocol_t* proto, void* foo_ctx, foo_protocol_ops_t* foo_ops) {
    const foo_protocol_t foo2 = {
        .ops = foo_ops,
        .ctx = foo_ctx,
    };
    const foo_protocol_t* foo = &foo2;
    proto->ops->world(proto->ctx, foo);
}


__END_CDECLS
