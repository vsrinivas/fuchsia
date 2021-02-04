// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.references banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct some_type some_type_t;
typedef struct in_out_protocol_protocol in_out_protocol_protocol_t;
typedef struct vector_field_in_struct vector_field_in_struct_t;
typedef struct mutable_field mutable_field_t;

// Declarations
struct some_type {
    uint32_t value;
};

typedef struct in_out_protocol_protocol_ops {
    void (*do_something)(void* ctx, some_type_t* param);
    void (*do_some_other_thing)(void* ctx, const some_type_t* param);
    void (*do_some_default_thing)(void* ctx, const some_type_t* param);
} in_out_protocol_protocol_ops_t;


struct in_out_protocol_protocol {
    in_out_protocol_protocol_ops_t* ops;
    void* ctx;
};

static inline void in_out_protocol_do_something(const in_out_protocol_protocol_t* proto, some_type_t* param) {
    proto->ops->do_something(proto->ctx, param);
}

static inline void in_out_protocol_do_some_other_thing(const in_out_protocol_protocol_t* proto, const some_type_t* param) {
    proto->ops->do_some_other_thing(proto->ctx, param);
}

static inline void in_out_protocol_do_some_default_thing(const in_out_protocol_protocol_t* proto, const some_type_t* param) {
    proto->ops->do_some_default_thing(proto->ctx, param);
}


struct vector_field_in_struct {
    const some_type_t** the_vector_list;
    size_t the_vector_count;
    const some_type_t* the_other_vector_list;
    size_t the_other_vector_count;
    const some_type_t* the_default_vector_list;
    size_t the_default_vector_count;
};

struct mutable_field {
    char* some_string;
    const char* some_other_string;
    const char* some_default_string;
};


__END_CDECLS
