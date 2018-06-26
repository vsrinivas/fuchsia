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

// page flip mode functions

zx_status_t fb_alloc_image_buffer(zx_handle_t* vmo_out);

// Imports a VMO handle as an image. This function always consumes |handle|. On
// success, the returned handle is guaranteed to not equal FB_INVALID_ID.
//
// If |type| is 0, the imported image has a linear memory layout. For any other
// values, it is the responsibility of the image producer and display driver to
// coordinate the meaning of |type|. All imported images must have the same type.
zx_status_t fb_import_image(zx_handle_t handle, uint32_t type, uint64_t *id_out);
void fb_release_image(uint64_t id);

// Imports an event handle to use for image synchronization. This function
// always consumes |handle|. Id must be unique and not equal to FB_INVALID_ID.
zx_status_t fb_import_event(zx_handle_t handle, uint64_t id);
void fb_release_event(uint64_t id);

// TODO(stevensd): Migrate clients to fb_present_image2 and delete this
zx_status_t fb_present_image(uint64_t image_id, uint64_t wait_event_id,
                             uint64_t present_event_id, uint64_t signal_event_id);

// Presents the image identified by |image_id|.
//
// If |wait_event_id| corresponds to an imported event, then driver will wait for
// for ZX_EVENT_SIGNALED before using the buffer. If |signal_event_id| corresponds
// to an imported event, then the driver will signal ZX_EVENT_SIGNALED when it is
// done with the image.
zx_status_t fb_present_image2(uint64_t image_id,
                             uint64_t wait_event_id, uint64_t signal_event_id);

#ifdef __cplusplus
}  // extern "C"
#endif
