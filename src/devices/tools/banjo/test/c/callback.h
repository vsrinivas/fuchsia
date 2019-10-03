// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.callback banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct point point_t;
typedef uint32_t direction_t;
#define DIRECTION_UP UINT32_C(0)
#define DIRECTION_DOWN UINT32_C(1)
#define DIRECTION_LEFT UINT32_C(2)
#define DIRECTION_RIGHT UINT32_C(3)
typedef struct drawing_protocol drawing_protocol_t;
typedef struct draw draw_t;

// Declarations
struct point {
    int32_t x;
    int32_t y;
};

typedef struct drawing_protocol_ops {
    void (*register_callback)(void* ctx);
    void (*deregister_callback)(void* ctx);
    int32_t (*draw_lots)(void* ctx, zx_handle_t commands, point_t* out_p);
    zx_status_t (*draw_array)(void* ctx, const point_t points[4]);
    void (*describe)(void* ctx, const char* one, char* out_two, size_t two_capacity);
} drawing_protocol_ops_t;


struct drawing_protocol {
    drawing_protocol_ops_t* ops;
    void* ctx;
};

static inline void drawing_register_callback(const drawing_protocol_t* proto) {
    proto->ops->register_callback(proto->ctx);
}

static inline void drawing_deregister_callback(const drawing_protocol_t* proto) {
    proto->ops->deregister_callback(proto->ctx);
}

static inline int32_t drawing_draw_lots(const drawing_protocol_t* proto, zx_handle_t commands, point_t* out_p) {
    return proto->ops->draw_lots(proto->ctx, commands, out_p);
}

static inline zx_status_t drawing_draw_array(const drawing_protocol_t* proto, const point_t points[4]) {
    return proto->ops->draw_array(proto->ctx, points);
}

static inline void drawing_describe(const drawing_protocol_t* proto, const char* one, char* out_two, size_t two_capacity) {
    proto->ops->describe(proto->ctx, one, out_two, two_capacity);
}


struct draw {
  void (*callback)(void* ctx, const point_t* p, direction_t d);
  void* ctx;
};


__END_CDECLS
