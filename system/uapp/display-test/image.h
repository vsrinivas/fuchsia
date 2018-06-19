// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/types.h>
#include <zircon/pixelformat.h>

// Intel only supports 90/270 rotation for Y-tiled images, so add
// a define to enable using it for testing.
#if defined(__x86_64__)
#define USE_INTEL_Y_TILING 1
#else
#define USE_INTEL_Y_TILING 0
#endif

#define TILE_PIXEL_WIDTH 32u
#define TILE_PIXEL_HEIGHT 32u
#define TILE_BYTES_PER_PIXEL 4u
#define TILE_NUM_BYTES 4096u
#define TILE_NUM_PIXELS (TILE_NUM_BYTES / TILE_BYTES_PER_PIXEL)
#define SUBTILE_COLUMN_WIDTH 4u


// Indicies into event and event_ids
#define WAIT_EVENT 0
#define PRESENT_EVENT 1
#define SIGNAL_EVENT 2

typedef struct image_import {
    uint64_t id;
    zx_handle_t events[3];
    uint64_t event_ids[3];
} image_import_t;

class Image {
public:
    static Image* Create(zx_handle_t dc_handle,
                         uint32_t width, uint32_t height, zx_pixel_format_t format,
                         uint32_t fg_color, bool cursor);

    void Render(int32_t prev_step, int32_t step_num);

    void* buffer() { return buf_; }
    uint32_t width() { return width_; }
    uint32_t height() { return height_; }
    uint32_t stride() { return stride_; }
    zx_pixel_format_t format() { return format_; }

    bool Import(zx_handle_t dc_handle, image_import_t* import_out);

private:
    Image(uint32_t width, uint32_t height, int32_t stride,
          zx_pixel_format_t format, zx_handle_t handle, void* buf,
          uint32_t fg_color, bool cursor);

    uint32_t width_;
    uint32_t height_;
    uint32_t stride_;
    zx_pixel_format_t format_;

    zx_handle_t vmo_;
    void* buf_;

    uint32_t fg_color_;
    bool cursor_;
};
