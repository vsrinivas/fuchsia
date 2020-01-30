// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_H_

//
//
//

#include "spinel_result.h"
#include "spinel_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// CONTEXT
//

spn_result_t
spn_context_retain(spn_context_t context);

spn_result_t
spn_context_release(spn_context_t context);

spn_result_t
spn_context_reset(spn_context_t context);

//
// STATUS
//

spn_result_t
spn_context_status(spn_context_t context);

//
// PATH BUILDER
//

spn_result_t
spn_path_builder_create(spn_context_t context, spn_path_builder_t * path_builder);

spn_result_t
spn_path_builder_retain(spn_path_builder_t path_builder);

spn_result_t
spn_path_builder_release(spn_path_builder_t path_builder);

spn_result_t
spn_path_builder_flush(spn_path_builder_t path_builder);

//
// PATH OPS
//

//
// TODO(allanmac) -- add a bulk/vectorized path func
// TODO(allanmac) -- add rational quad and cubic funcs
// TODO(allanmac) -- move to a "spn_path_<op>(pb,xy[])" proto
//

spn_result_t
spn_path_builder_begin(spn_path_builder_t path_builder);

spn_result_t
spn_path_builder_end(spn_path_builder_t path_builder, spn_path_t * path);

spn_result_t
spn_path_builder_move_to(spn_path_builder_t path_builder, float x0, float y0);

spn_result_t
spn_path_builder_line_to(spn_path_builder_t path_builder, float x1, float y1);

spn_result_t
spn_path_builder_cubic_to(spn_path_builder_t path_builder,  //
                          float              x1,
                          float              y1,
                          float              x2,
                          float              y2,
                          float              x3,
                          float              y3);

spn_result_t
spn_path_builder_cubic_smooth_to(spn_path_builder_t path_builder,  //
                                 float              x2,
                                 float              y2,
                                 float              x3,
                                 float              y3);

spn_result_t
spn_path_builder_quad_to(spn_path_builder_t path_builder,  //
                         float              x1,
                         float              y1,
                         float              x2,
                         float              y2);

spn_result_t
spn_path_builder_quad_smooth_to(spn_path_builder_t path_builder,  //
                                float              x2,
                                float              y2);

spn_result_t
spn_path_builder_rat_quad_to(spn_path_builder_t path_builder,  //
                             float              x1,
                             float              y1,
                             float              x2,
                             float              y2,
                             float              w1);

spn_result_t
spn_path_builder_rat_cubic_to(spn_path_builder_t path_builder,
                              float              x1,
                              float              y1,
                              float              x2,
                              float              y2,
                              float              x3,
                              float              y3,
                              float              w1,
                              float              w2);

//
// TODO(allanmac) -- this is a synthetic built from primitives and doesn't
// belong here
//

spn_result_t
spn_path_builder_ellipse(spn_path_builder_t path_builder, float cx, float cy, float rx, float ry);

//
// PATH RETAIN/RELEASE
//

spn_result_t
spn_path_retain(spn_context_t context, spn_path_t const * paths, uint32_t count);

spn_result_t
spn_path_release(spn_context_t context, spn_path_t const * paths, uint32_t count);

//
// RASTER BUILDER
//

spn_result_t
spn_raster_builder_create(spn_context_t context, spn_raster_builder_t * raster_builder);

spn_result_t
spn_raster_builder_retain(spn_raster_builder_t raster_builder);

spn_result_t
spn_raster_builder_release(spn_raster_builder_t raster_builder);

spn_result_t
spn_raster_builder_flush(spn_raster_builder_t raster_builder);

//
// RASTER OPS
//

spn_result_t
spn_raster_builder_begin(spn_raster_builder_t raster_builder);

spn_result_t
spn_raster_builder_end(spn_raster_builder_t raster_builder, spn_raster_t * raster);

spn_result_t
spn_raster_builder_add(spn_raster_builder_t      raster_builder,
                       spn_path_t const *        paths,
                       spn_transform_weakref_t * transform_weakrefs,
                       spn_transform_t const *   transforms,
                       spn_clip_weakref_t *      clip_weakrefs,
                       spn_clip_t const *        clips,
                       uint32_t                  count);

//
// RASTER RETAIN/RELEASE
//

spn_result_t
spn_raster_retain(spn_context_t context, spn_raster_t const * rasters, uint32_t count);

spn_result_t
spn_raster_release(spn_context_t context, spn_raster_t const * rasters, uint32_t count);

//
// COMPOSITION STATE
//

spn_result_t
spn_composition_create(spn_context_t context, spn_composition_t * composition);

spn_result_t
spn_composition_clone(spn_context_t       context,
                      spn_composition_t   composition,
                      spn_composition_t * clone);

spn_result_t
spn_composition_retain(spn_composition_t composition);

spn_result_t
spn_composition_release(spn_composition_t composition);

//
// TODO(allanmac): evaluate a per-place clip
// TODO(allanmac): implement "tile activation" clear
//

spn_result_t
spn_composition_place(spn_composition_t    composition,
                      spn_raster_t const * rasters,
                      spn_layer_id const * layer_ids,
                      spn_txty_t const *   txtys,
                      uint32_t             count);

spn_result_t
spn_composition_seal(spn_composition_t composition);

spn_result_t
spn_composition_unseal(spn_composition_t composition);

spn_result_t
spn_composition_reset(spn_composition_t composition);

spn_result_t
spn_composition_get_bounds(spn_composition_t composition, uint32_t bounds[4]);

spn_result_t
spn_composition_set_clip(spn_composition_t composition, uint32_t const clip[4]);

//
// TODO: COMPOSITION CLIP
//
// Produce a new composition from a simple clip rect.
//

//
// TODO: COMPOSITION "BOOLEAN ALGEBRA" OPERATIONS
//
// Produce a new composition from the union or intersection of two
// existing compositions.
//

//
// TODO: COMPOSITION "HIT DETECTION"
//
// Report which layers and tiles are intersected by one or more
// device-space (x,y) points
//

//
// STYLING STATE
//

spn_result_t
spn_styling_create(spn_context_t   context,
                   spn_styling_t * styling,
                   uint32_t        layers_count,
                   uint32_t        cmds_count);

spn_result_t
spn_styling_retain(spn_styling_t styling);

spn_result_t
spn_styling_release(spn_styling_t styling);

spn_result_t
spn_styling_seal(spn_styling_t styling);

spn_result_t
spn_styling_unseal(spn_styling_t styling);

spn_result_t
spn_styling_reset(spn_styling_t styling);

//
// STYLING GROUPS AND LAYERS
//

spn_result_t
spn_styling_group_alloc(spn_styling_t styling, spn_group_id * const group_id);

spn_result_t
spn_styling_group_enter(spn_styling_t      styling,
                        spn_group_id const group_id,
                        uint32_t const     n,
                        uint32_t ** const  cmds);

spn_result_t
spn_styling_group_leave(spn_styling_t      styling,
                        spn_group_id const group_id,
                        uint32_t const     n,
                        uint32_t ** const  cmds);
//
// n:
//
//   The number of parent groups above this group.
//
// parents:
//
//   The sequence of parent group ids leading from the top of
//   hierarchy to the parent of 'group_id'.
//

spn_result_t
spn_styling_group_parents(spn_styling_t      styling,
                          spn_group_id const group_id,
                          uint32_t const     n,
                          uint32_t ** const  parents);

spn_result_t
spn_styling_group_range_lo(spn_styling_t      styling,
                           spn_group_id const group_id,
                           spn_layer_id const layer_lo);

spn_result_t
spn_styling_group_range_hi(spn_styling_t      styling,
                           spn_group_id const group_id,
                           spn_layer_id const layer_hi);

spn_result_t
spn_styling_group_layer(spn_styling_t              styling,
                        spn_group_id const         group_id,
                        spn_layer_id const         layer_id,
                        uint32_t const             n,
                        spn_styling_cmd_t ** const cmds);

//
// TODO(allanmac) -- styling command encoders will be opaque
//

void
spn_styling_layer_fill_rgba_encoder(spn_styling_cmd_t * cmds, float const rgba[4]);

void
spn_styling_background_over_encoder(spn_styling_cmd_t * cmds, float const rgba[4]);

//
//
//

#ifdef SPN_DISABLE_UNTIL_INTEGRATED

void
spn_styling_layer_fill_gradient_encoder(spn_styling_cmd_t *         cmds,
                                        float                       x0,
                                        float                       y0,
                                        float                       x1,
                                        float                       y1,
                                        spn_styling_gradient_type_e type,
                                        uint32_t                    n,
                                        float const                 stops[],
                                        float const                 colors[]);
#endif

//
// RENDER
//

spn_result_t
spn_render(spn_context_t context, spn_render_submit_t const * const submit);

//
// COORDINATED EXTERNAL OPERATIONS
//
//  Examples include:
//
//  - Transforming an intermediate layer with a blur, sharpen, rotation or scaling kernel.
//  - Subpixel antialiasing using neighboring pixel color and coverage data.
//  - Performing a blit from one region to another region on a surface.
//  - Blitting from one surface to another.
//  - Loading and processing from one region and storing to another region.
//  - Rendezvousing with an external pipeline.
//

// FORTHCOMING...

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_H_
