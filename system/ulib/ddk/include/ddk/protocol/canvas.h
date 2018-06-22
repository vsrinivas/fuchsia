// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t wrap;
    uint32_t blkmode;
    uint32_t endianess;
} canvas_info_t;

typedef struct {
    zx_status_t (*config)(void* ctx, zx_handle_t vmo,
                 size_t offset, canvas_info_t* info,
                 uint8_t* canvas_idx);
    zx_status_t (*free)(void* ctx, uint8_t canvas_idx);
} canvas_protocol_ops_t;

typedef struct {
    canvas_protocol_ops_t* ops;
    void* ctx;
} canvas_protocol_t;

// Configures a canvas
// Adds a framebuffer to the canvas lookup table
static inline zx_status_t canvas_config(canvas_protocol_t* canvas, zx_handle_t vmo,
                                        size_t offset, canvas_info_t* info,
                                        uint8_t* canvas_idx) {
    return canvas->ops->config(canvas->ctx, vmo, offset,
                               info, canvas_idx);
}

// Frees up a canvas
static inline zx_status_t canvas_free(canvas_protocol_t* canvas, uint8_t canvas_idx) {
    return canvas->ops->free(canvas->ctx, canvas_idx);
}
__END_CDECLS;
