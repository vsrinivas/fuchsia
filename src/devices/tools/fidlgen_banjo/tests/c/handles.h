// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.handles banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct container container_t;
typedef struct doer_protocol doer_protocol_t;
typedef struct doer_protocol_ops doer_protocol_ops_t;

// Declarations
struct container {
    zx_handle_t a_handle;
    zx_handle_t another_handle;
};

struct doer_protocol_ops {
    void (*do_something)(void* ctx, zx_handle_t the_handle);
    void (*do_something_else)(void* ctx, zx_handle_t the_handle_too);
};


struct doer_protocol {
    doer_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
static inline void doer_do_something(const doer_protocol_t* proto, zx_handle_t the_handle) {
    proto->ops->do_something(proto->ctx, the_handle);
}

static inline void doer_do_something_else(const doer_protocol_t* proto, zx_handle_t the_handle_too) {
    proto->ops->do_something_else(proto->ctx, the_handle_too);
}


__END_CDECLS
