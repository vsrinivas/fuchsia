// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.buffer banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct some_methods_protocol some_methods_protocol_t;
typedef struct some_methods_protocol_ops some_methods_protocol_ops_t;
typedef struct some_data some_data_t;

// Declarations
struct some_methods_protocol_ops {
    void (*do_something)(void* ctx, const uint8_t* input_buffer, size_t input_size);
};


struct some_methods_protocol {
    some_methods_protocol_ops_t* ops;
    void* ctx;
};

struct some_data {
    const uint8_t* one_buffer;
    size_t one_size;
};


// Helpers
static inline void some_methods_do_something(const some_methods_protocol_t* proto, const uint8_t* input_buffer, size_t input_size) {
    proto->ops->do_something(proto->ctx, input_buffer, input_size);
}


__END_CDECLS
