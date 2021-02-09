// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.order3 banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef uint32_t foo_t;
#define FOO_HELLO UINT32_C(1)
typedef struct bar_protocol bar_protocol_t;
typedef struct bar_protocol_ops bar_protocol_ops_t;

// Declarations
struct bar_protocol_ops {
    void (*world)(void* ctx, foo_t foo);
};


struct bar_protocol {
    bar_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
static inline void bar_world(const bar_protocol_t* proto, foo_t foo) {
    proto->ops->world(proto->ctx, foo);
}


__END_CDECLS
