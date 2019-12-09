// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOLD_H
#define MOLD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Mold implements the Spinel API, except for the following important
// differences:
//
//   1) Creation a new Spinel context should be performed with
//      mold_context_create(), instead of spn_vk_context_create() since the
//      latter is very specific to Vulkan-specific version of Spinel.
//
//   2) To render into a target buffer, call spn_render() with a submit
//      extension pointer (i.e. the spn_render_submit_t::ext field) that points
//      to a mold_raw_buffer_t struct describing the target buffer. E.g.:
//
//         const mold_raw_buffer_t target_buffer = {
//             .buffer = ...
//             .width  = BUFFER_WIDTH,
//             .format = MOLD_RGBA8888,
//         };
//
//         const spn_render_submit_t submit = {
//             .ext = &target_buffer,
//             .styling = ...,
//             .composition = ...,
//             .clip = ...,
//         };
//         spn_render(context, &submit);
//

#include "spinel/spinel_types.h"

// Supported target buffer pixel formats.
typedef enum
{
  MOLD_RGBA8888,
  MOLD_BGRA8888,
  MOLD_RGB565,
} mold_pixel_format_t;

// When calling spn_render(), the |spn_render_submit_t::ext| field should
// point to a mold_raw_buffer_t.
//
// |buffer| points to the start of the pixel buffer.
// |width| is the width of each scanline in pixels.
// |format| describes the format of each pixel.
typedef struct mold_raw_buffer_t
{
  void *              buffer;
  size_t              width;
  mold_pixel_format_t format;
} mold_raw_buffer_t;

// Create new Mold context to be used with the Spinel API.
// Mold otherwise implements the other spn_xxx() functions.
void
mold_context_create(spn_context_t * context);

#ifdef __cplusplus
}
#endif

#endif  // MOLD_H
