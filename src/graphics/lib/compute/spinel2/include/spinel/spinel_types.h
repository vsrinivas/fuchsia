// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_TYPES_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// clang-format off
//

typedef struct spinel_context         * spinel_context_t;
typedef struct spinel_path_builder    * spinel_path_builder_t;
typedef struct spinel_raster_builder  * spinel_raster_builder_t;
typedef struct spinel_composition     * spinel_composition_t;
typedef struct spinel_styling         * spinel_styling_t;
typedef struct spinel_swapchain       * spinel_swapchain_t;

typedef uint32_t                        spinel_layer_id; // TODO(allanmac): slated for removal
typedef uint32_t                        spinel_group_id; // TODO(allanmac): slated for removal

typedef uint32_t                        spinel_styling_cmd_t;
typedef uint32_t                        spinel_handle_t;

//
//
//

typedef struct spinel_path              { spinel_handle_t handle; }  spinel_path_t;
typedef struct spinel_raster            { spinel_handle_t handle; }  spinel_raster_t;

typedef struct spinel_transform_weakref { uint32_t weakref[2]; }     spinel_transform_weakref_t;
typedef struct spinel_clip_weakref      { uint32_t weakref[2]; }     spinel_clip_weakref_t;

//
//
//

#define SPN_PATH_INVALID                ((spinel_path_t)  { .handle = UINT32_MAX })
#define SPN_RASTER_INVALID              ((spinel_raster_t){ .handle = UINT32_MAX })

#define SPN_TRANSFORM_WEAKREF_INVALID   ((spinel_transform_weakref_t){ .weakref = { UINT32_MAX, UINT32_MAX } })
#define SPN_CLIP_WEAKREF_INVALID        ((spinel_clip_weakref_t)     { .weakref = { UINT32_MAX, UINT32_MAX } })

//
// clang-format on
//

//
// TRANSFORMS
//
// Spinel supports a projective transformation matrix with the
// requirement that w2 is implicitly 1.
//
//   A---------B----+
//   | sx  shx | tx |
//   | shy sy  | ty |
//   C---------D----+
//   | w0  w1  | 1  |
//   +---------+----+
//
// The layout of the transform is defined by the spinel_transform_t struct.
//
// It's the responsibility of the host to ensure that the transforms
// are properly scaled either via initializing a transform stack with
// the transform returned by `spinel_context_get_limits()`.
//
typedef struct spinel_transform
{
  float sx;
  float shx;
  float tx;
  float shy;
  float sy;
  float ty;
  float w0;
  float w1;
} spinel_transform_t;

//
// RASTERIZATION CLIP
//
// The coordinate clip rectangle is used by `raster_builder_add()`.
//
typedef struct spinel_clip
{
  float x0;
  float y0;
  float x1;
  float y1;
} spinel_clip_t;

//
// PIXEL CLIP
//
// The coordinate clip rectangle is used by `raster_builder_add()`.
//
typedef struct spinel_pixel_clip
{
  uint32_t x0;
  uint32_t y0;
  uint32_t x1;
  uint32_t y1;
} spinel_pixel_clip_t;

//
// TXTY LAYOUT: { tx, ty }
//
// FIXME(allanmac): It may be necessary to make tx/ty floats.
//
typedef struct spinel_txty
{
  int32_t tx;
  int32_t ty;
} spinel_txty_t;

//
// EXTENT 2D
//
typedef struct spinel_extent_2d
{
  uint32_t width;
  uint32_t height;
} spinel_extent_2d_t;

//
// LIMITS
//
//  .global_transform - Mandatory global transform
//  .tile             - Tile size in pixels
//  .extent           - Max rendering extent size
//
typedef struct spinel_context_limits
{
  spinel_transform_t global_transform;
  spinel_extent_2d_t tile;
  spinel_extent_2d_t extent;
} spinel_context_limits_t;

//
// STYLING CREATE
//
typedef struct spinel_styling_create_info
{
  uint32_t layer_count;
  uint32_t cmd_count;
} spinel_styling_create_info_t;

//
// SWAPCHAIN CREATE
//
//  .extent - size of surface
//  .count  - number of surfaces
//
typedef struct spinel_swapchain_create_info
{
  spinel_extent_2d_t extent;
  uint32_t           count;
} spinel_swapchain_create_info_t;

//
// SWAPCHAIN SUBMIT
//
// Submits a composition and styling and platform-specific extensions to the
// swapchain.
//
typedef struct spinel_swapchain_submit
{
  void *               ext;
  spinel_styling_t     styling;
  spinel_composition_t composition;
} spinel_swapchain_submit_t;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_TYPES_H_
