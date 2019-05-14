// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_TYPES_H_

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

typedef struct spn_context        * spn_context_t;
typedef struct spn_path_builder   * spn_path_builder_t;
typedef struct spn_raster_builder * spn_raster_builder_t;

typedef struct spn_composition    * spn_composition_t;
typedef struct spn_styling        * spn_styling_t;

typedef struct spn_surface        * spn_surface_t;

typedef        uint32_t             spn_path_t;
typedef        uint32_t             spn_raster_t;

typedef        uint32_t             spn_layer_id;
typedef        uint32_t             spn_group_id;

typedef        uint32_t             spn_styling_cmd_t;

typedef        uint64_t             spn_weakref_t;
typedef        spn_weakref_t        spn_transform_weakref_t;
typedef        spn_weakref_t        spn_clip_weakref_t;

//
//
//

#define SPN_PATH_INVALID               UINT32_MAX
#define SPN_RASTER_INVALID             UINT32_MAX

#define SPN_WEAKREF_INVALID            0UL
#define SPN_TRANSFORM_WEAKREF_INVALID  SPN_WEAKREF_INVALID
#define SPN_CLIP_WEAKREF_INVALID       SPN_WEAKREF_INVALID

//
// clang-format on
//

//
// TRANSFORM LAYOUT: { sx shx tx shy sy ty w0 w1 }
//

//
// RASTER CLIP LAYOUT: { x0, y0, x1, y1 }
//

//
// RENDER
//
// Render a composition and styling to a surface defined in the
// extension chain.
//

typedef struct spn_render_submit
{
  void *            ext;
  spn_styling_t     styling;
  spn_composition_t composition;
  uint32_t          tile_clip[4];
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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_TYPES_H_
