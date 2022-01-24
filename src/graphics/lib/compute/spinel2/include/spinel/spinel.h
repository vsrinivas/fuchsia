// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_H_

//
//
//

#include "spinel/spinel_result.h"
#include "spinel/spinel_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// CONTEXT
//

spinel_result_t
spinel_context_retain(spinel_context_t context);

spinel_result_t
spinel_context_release(spinel_context_t context);

spinel_result_t
spinel_context_reset(spinel_context_t context);

spinel_result_t
spinel_context_get_limits(spinel_context_t context, spinel_context_limits_t * limits);

//
// PATH BUILDER
//

spinel_result_t
spinel_path_builder_create(spinel_context_t context, spinel_path_builder_t * path_builder);

spinel_result_t
spinel_path_builder_retain(spinel_path_builder_t path_builder);

spinel_result_t
spinel_path_builder_release(spinel_path_builder_t path_builder);

spinel_result_t
spinel_path_builder_flush(spinel_path_builder_t path_builder);

//
// PATH OPS
//

spinel_result_t
spinel_path_builder_begin(spinel_path_builder_t path_builder);

spinel_result_t
spinel_path_builder_end(spinel_path_builder_t path_builder, spinel_path_t * path);

spinel_result_t
spinel_path_builder_move_to(spinel_path_builder_t path_builder, float x0, float y0);

spinel_result_t
spinel_path_builder_line_to(spinel_path_builder_t path_builder, float x1, float y1);

spinel_result_t
spinel_path_builder_cubic_to(spinel_path_builder_t path_builder,  //
                             float                 x1,
                             float                 y1,
                             float                 x2,
                             float                 y2,
                             float                 x3,
                             float                 y3);

spinel_result_t
spinel_path_builder_cubic_smooth_to(spinel_path_builder_t path_builder,  //
                                    float                 x2,
                                    float                 y2,
                                    float                 x3,
                                    float                 y3);

spinel_result_t
spinel_path_builder_quad_to(spinel_path_builder_t path_builder,  //
                            float                 x1,
                            float                 y1,
                            float                 x2,
                            float                 y2);

spinel_result_t
spinel_path_builder_quad_smooth_to(spinel_path_builder_t path_builder,  //
                                   float                 x2,
                                   float                 y2);

spinel_result_t
spinel_path_builder_rat_quad_to(spinel_path_builder_t path_builder,  //
                                float                 x1,
                                float                 y1,
                                float                 x2,
                                float                 y2,
                                float                 w1);

spinel_result_t
spinel_path_builder_rat_cubic_to(spinel_path_builder_t path_builder,  //
                                 float                 x1,
                                 float                 y1,
                                 float                 x2,
                                 float                 y2,
                                 float                 x3,
                                 float                 y3,
                                 float                 w1,
                                 float                 w2);

//
// PATH RETAIN/RELEASE
//

spinel_result_t
spinel_path_retain(spinel_context_t context, spinel_path_t const * paths, uint32_t count);

spinel_result_t
spinel_path_release(spinel_context_t context, spinel_path_t const * paths, uint32_t count);

//
// RASTER BUILDER
//

spinel_result_t
spinel_raster_builder_create(spinel_context_t context, spinel_raster_builder_t * raster_builder);

spinel_result_t
spinel_raster_builder_retain(spinel_raster_builder_t raster_builder);

spinel_result_t
spinel_raster_builder_release(spinel_raster_builder_t raster_builder);

spinel_result_t
spinel_raster_builder_flush(spinel_raster_builder_t raster_builder);

//
// RASTER OPS
//

spinel_result_t
spinel_raster_builder_begin(spinel_raster_builder_t raster_builder);

spinel_result_t
spinel_raster_builder_end(spinel_raster_builder_t raster_builder, spinel_raster_t * raster);

spinel_result_t
spinel_raster_builder_add(spinel_raster_builder_t      raster_builder,
                          spinel_path_t const *        paths,
                          spinel_transform_weakref_t * transform_weakrefs,
                          spinel_transform_t const *   transforms,
                          spinel_clip_weakref_t *      clip_weakrefs,
                          spinel_clip_t const *        clips,
                          uint32_t                     count);

//
// RASTER RETAIN/RELEASE
//

spinel_result_t
spinel_raster_retain(spinel_context_t context, spinel_raster_t const * rasters, uint32_t count);

spinel_result_t
spinel_raster_release(spinel_context_t context, spinel_raster_t const * rasters, uint32_t count);

//
// COMPOSITION STATE
//

spinel_result_t
spinel_composition_create(spinel_context_t context, spinel_composition_t * composition);

spinel_result_t
spinel_composition_retain(spinel_composition_t composition);

spinel_result_t
spinel_composition_release(spinel_composition_t composition);

//
// TODO(allanmac): evaluate a per-place clip
// TODO(allanmac): implement "tile activation" clear
//

spinel_result_t
spinel_composition_place(spinel_composition_t    composition,
                         spinel_raster_t const * rasters,
                         spinel_layer_id const * layer_ids,
                         spinel_txty_t const *   txtys,
                         uint32_t                count);

spinel_result_t
spinel_composition_seal(spinel_composition_t composition);

spinel_result_t
spinel_composition_unseal(spinel_composition_t composition);

spinel_result_t
spinel_composition_reset(spinel_composition_t composition);

spinel_result_t
spinel_composition_set_clip(spinel_composition_t composition, spinel_pixel_clip_t const * clip);

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

spinel_result_t
spinel_styling_create(spinel_context_t                     context,
                      spinel_styling_create_info_t const * create_info,
                      spinel_styling_t *                   styling);

spinel_result_t
spinel_styling_retain(spinel_styling_t styling);

spinel_result_t
spinel_styling_release(spinel_styling_t styling);

spinel_result_t
spinel_styling_seal(spinel_styling_t styling);

spinel_result_t
spinel_styling_unseal(spinel_styling_t styling);

spinel_result_t
spinel_styling_reset(spinel_styling_t styling);

//
// STYLING GROUPS AND LAYERS
//

spinel_result_t
spinel_styling_group_alloc(spinel_styling_t styling, spinel_group_id * const group_id);

spinel_result_t
spinel_styling_group_enter(spinel_styling_t      styling,
                           spinel_group_id const group_id,
                           uint32_t const        n,
                           uint32_t ** const     cmds);

spinel_result_t
spinel_styling_group_leave(spinel_styling_t      styling,
                           spinel_group_id const group_id,
                           uint32_t const        n,
                           uint32_t ** const     cmds);
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

spinel_result_t
spinel_styling_group_parents(spinel_styling_t      styling,
                             spinel_group_id const group_id,
                             uint32_t const        n,
                             uint32_t ** const     parents);

spinel_result_t
spinel_styling_group_range_lo(spinel_styling_t      styling,
                              spinel_group_id const group_id,
                              spinel_layer_id const layer_lo);

spinel_result_t
spinel_styling_group_range_hi(spinel_styling_t      styling,
                              spinel_group_id const group_id,
                              spinel_layer_id const layer_hi);

spinel_result_t
spinel_styling_group_layer(spinel_styling_t              styling,
                           spinel_group_id const         group_id,
                           spinel_layer_id const         layer_id,
                           uint32_t const                n,
                           spinel_styling_cmd_t ** const cmds);

//
// TODO(allanmac) -- styling command encoders will be opaque
//

void
spinel_styling_layer_fill_rgba_encoder(spinel_styling_cmd_t * cmds, float const rgba[4]);

void
spinel_styling_background_over_encoder(spinel_styling_cmd_t * cmds, float const rgba[4]);

//
//
//

#ifdef SPN_DISABLE_UNTIL_INTEGRATED

void
spinel_styling_layer_fill_gradient_encoder(spinel_styling_cmd_t *         cmds,
                                           float                          x0,
                                           float                          y0,
                                           float                          x1,
                                           float                          y1,
                                           spinel_styling_gradient_type_e type,
                                           uint32_t                       n,
                                           float const                    stops[],
                                           float const                    colors[]);
#endif

//
// SWAPCHAIN
//

spinel_result_t
spinel_swapchain_create(spinel_context_t                       context,
                        spinel_swapchain_create_info_t const * create_info,
                        spinel_swapchain_t *                   swapchain);

spinel_result_t
spinel_swapchain_retain(spinel_swapchain_t swapchain);

spinel_result_t
spinel_swapchain_release(spinel_swapchain_t swapchain);

spinel_result_t
spinel_swapchain_submit(spinel_swapchain_t                swapchain,  //
                        spinel_swapchain_submit_t const * submit);

//
// TODO(allanmac): COORDINATED EXTERNAL OPERATIONS
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

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_H_
