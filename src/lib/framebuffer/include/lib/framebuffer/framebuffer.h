// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FRAMEBUFFER_INCLUDE_LIB_FRAMEBUFFER_FRAMEBUFFER_H_
#define SRC_LIB_FRAMEBUFFER_INCLUDE_LIB_FRAMEBUFFER_FRAMEBUFFER_H_

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
void fb_get_config(uint32_t* width_out, uint32_t* height_out, uint32_t* linear_stride_px_out,
                   zx_pixel_format_t* format_out);

// single buffer mode functions

// Returns a VMO handle to the buffer being displayed. The client does not
// own the returned handle.
zx_handle_t fb_get_single_buffer(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SRC_LIB_FRAMEBUFFER_INCLUDE_LIB_FRAMEBUFFER_FRAMEBUFFER_H_
