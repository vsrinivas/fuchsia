// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.pass.callback banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct point point_t;
typedef uint8_t direction_t;
#define DIRECTION_UP UINT8_C(1)
#define DIRECTION_DOWN UINT8_C(2)
#define DIRECTION_LEFT UINT8_C(3)
#define DIRECTION_RIGHT UINT8_C(4)
typedef struct action_notify action_notify_t;
typedef struct action_protocol_protocol action_protocol_protocol_t;
typedef uint32_t action_t;
#define ACTION_START UINT32_C(0x1)
#define ACTION_STOP UINT32_C(0x2)

// Declarations
struct point {
    int64_t x;
    int64_t y;
};

struct action_notify {
  void (*callback)(void* ctx, const point_t* p, direction_t d);
  void* ctx;
};

typedef struct action_protocol_protocol_ops {
    zx_status_t (*register_callback)(void* ctx, uint32_t id, const action_notify_t* cb);
    zx_status_t (*get_callback)(void* ctx, uint32_t id, action_notify_t* out_cb);
} action_protocol_protocol_ops_t;


struct action_protocol_protocol {
    action_protocol_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t action_protocol_register_callback(const action_protocol_protocol_t* proto, uint32_t id, const action_notify_t* cb) {
    return proto->ops->register_callback(proto->ctx, id, cb);
}

static inline zx_status_t action_protocol_get_callback(const action_protocol_protocol_t* proto, uint32_t id, action_notify_t* out_cb) {
    return proto->ops->get_callback(proto->ctx, id, out_cb);
}



__END_CDECLS
