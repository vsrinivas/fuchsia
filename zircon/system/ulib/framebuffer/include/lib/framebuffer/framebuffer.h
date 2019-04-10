// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <zircon/pixelformat.h>
#include <zircon/types.h>

#define FB_INVALID_ID 0

// Binds to the display. |single_buffer| determines whether the connection
// is initialized in single buffer or page flip mode.
zx_status_t fb_bind(bool single_buffer, const char** err_msg);

void fb_release(void);

// Gets the framebuffer configuration. In single buffer mode, this is the
// configuration of the allocated buffer. In page flip mode, imported images
// must have this configuration.
void fb_get_config(uint32_t* width_out, uint32_t* height_out,
                   uint32_t* linear_stride_px_out, zx_pixel_format_t* format_out);

// single buffer mode functions

// Returns a VMO handle to the buffer being displayed. The client does not
// own the returned handle.
zx_handle_t fb_get_single_buffer(void);

// vsync functions

// Enable vsync for page flip mode.
zx_status_t fb_enable_vsync(bool enable);

// Wait for vsync event. VSync time is returned in |timestamp| and scanned out
// image in |image_id|.
zx_status_t fb_wait_for_vsync(zx_time_t* timestamp, uint64_t* image_id);

#ifdef __cplusplus
}  // extern "C"
#endif
