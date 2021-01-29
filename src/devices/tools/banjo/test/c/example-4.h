// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.example4 banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct interface_protocol interface_protocol_t;
typedef uint32_t enum_t;
#define ENUM_X UINT32_C(23)
typedef struct point point_t;

// Declarations
typedef struct interface_protocol_ops {
    void (*func)(void* ctx, bool x);
} interface_protocol_ops_t;


struct interface_protocol {
    interface_protocol_ops_t* ops;
    void* ctx;
};

static inline void interface_func(const interface_protocol_t* proto, bool x) {
    proto->ops->func(proto->ctx, x);
}


struct point {
    uint64_t x;
};


__END_CDECLS
