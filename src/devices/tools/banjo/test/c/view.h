// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.view banjo file

#pragma once

#include <banjo/examples/point/c/banjo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct view_protocol view_protocol_t;
typedef struct view_protocol_ops view_protocol_ops_t;

// Declarations
struct view_protocol_ops {
    void (*move_to)(void* ctx, const point_t* p);
};


struct view_protocol {
    view_protocol_ops_t* ops;
    void* ctx;
};


// Helpers
static inline void view_move_to(const view_protocol_t* proto, const point_t* p) {
    proto->ops->move_to(proto->ctx, p);
}


__END_CDECLS
