// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/amlogic_canvas.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct canvas_info canvas_info_t;
typedef struct canvas_protocol canvas_protocol_t;

// Declarations

struct canvas_info {
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t wrap;
    uint32_t blkmode;
    uint32_t endianness;
};

typedef struct canvas_protocol_ops {
    zx_status_t (*config)(void* ctx, zx_handle_t vmo, size_t offset, const canvas_info_t* info,
                          uint8_t* out_canvas_idx);
    zx_status_t (*free)(void* ctx, uint8_t canvas_idx);
} canvas_protocol_ops_t;

struct canvas_protocol {
    canvas_protocol_ops_t* ops;
    void* ctx;
};

// Configures a canvas.
// Adds a framebuffer to the canvas lookup table.
static inline zx_status_t canvas_config(const canvas_protocol_t* proto, zx_handle_t vmo,
                                        size_t offset, const canvas_info_t* info,
                                        uint8_t* out_canvas_idx) {
    return proto->ops->config(proto->ctx, vmo, offset, info, out_canvas_idx);
}
// Frees up a canvas.
static inline zx_status_t canvas_free(const canvas_protocol_t* proto, uint8_t canvas_idx) {
    return proto->ops->free(proto->ctx, canvas_idx);
}

__END_CDECLS;
