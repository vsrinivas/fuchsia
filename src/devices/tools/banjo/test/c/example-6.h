// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example6 banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
#define x INT32_C(23)
typedef struct hello_protocol hello_protocol_t;
typedef struct hello_protocol_ops hello_protocol_ops_t;

// Declarations
struct hello_protocol_ops {
    void (*say)(void* ctx, const char* req, char* out_response, size_t response_capacity);
};


struct hello_protocol {
    hello_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
static inline void hello_say(const hello_protocol_t* proto, const char* req, char* out_response, size_t response_capacity) {
    proto->ops->say(proto->ctx, req, out_response, response_capacity);
}


__END_CDECLS
