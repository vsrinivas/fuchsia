// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_TYPES_H_

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

typedef struct spn_context         * spn_context_t;
typedef struct spn_path_builder    * spn_path_builder_t;
typedef struct spn_raster_builder  * spn_raster_builder_t;
typedef struct spn_composition     * spn_composition_t;
typedef struct spn_styling         * spn_styling_t;
typedef struct spn_surface         * spn_surface_t;

typedef uint32_t                     spn_layer_id;
typedef uint32_t                     spn_group_id;
typedef uint32_t                     spn_styling_cmd_t;

//
//
//

typedef struct spn_path              { uint32_t handle; }     spn_path_t;
typedef struct spn_raster            { uint32_t handle; }     spn_raster_t;

typedef struct spn_transform_weakref { uint32_t weakref[2]; } spn_transform_weakref_t;
typedef struct spn_clip_weakref      { uint32_t weakref[2]; } spn_clip_weakref_t;

//
//
//

#define SPN_PATH_INVALID                ((spn_path_t)  { .handle = UINT32_MAX })
#define SPN_RASTER_INVALID              ((spn_raster_t){ .handle = UINT32_MAX })

#define SPN_TRANSFORM_WEAKREF_INVALID   ((spn_transform_weakref_t){ .weakref = { UINT32_MAX, UINT32_MAX } })
#define SPN_CLIP_WEAKREF_INVALID        ((spn_clip_weakref_t)     { .weakref = { UINT32_MAX, UINT32_MAX } })

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
// The column-ordered transformation matrix can be initialized with
// the array:
//
//   LAYOUT: { sx shx tx shy sy ty w0 w1 }
//
// Spinel requires that all transforms are globally scaled by 32.
//
// It's the responsibility of the host to ensure that the transforms
// are properly scaled either via intitializing a transform stack with
// the a scaled identity or scaling the transform before it is
// submitted to spn_raster_fill().
//

typedef struct spn_transform
{
  float sx;
  float shx;
  float tx;
  float shy;
  float sy;
  float ty;
  float w0;
  float w1;
} spn_transform_t;

//
// CLIP LAYOUT: { x0, y0, x1, y1 }
//
// Currently only used by rasters.  The composition will use an
// integer clip.
//

typedef struct spn_clip
{
  float x0;
  float y0;
  float x1;
  float y1;
} spn_clip_t;

//
// TXTY LAYOUT: { tx, ty }
//
// FIXME(allanmac): it's now reasonable to make tx/ty floats.
//

typedef struct spn_txty
{
  int32_t tx;
  int32_t ty;
} spn_txty_t;

//
// RENDER
//
// Render a composition and styling to a surface defined in the
// extension chain.
//
// The clip is in pixels.
//

typedef struct spn_render_submit
{
  void *            ext;
  spn_styling_t     styling;
  spn_composition_t composition;
  uint32_t          clip[4];
} spn_render_submit_t;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_TYPES_H_
