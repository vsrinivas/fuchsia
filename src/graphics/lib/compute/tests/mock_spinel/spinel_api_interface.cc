// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_api_interface.h"

#include <stdio.h>
#include <stdlib.h>

#include "tests/common/utils.h"  // For ASSERT_MSG()

using spinel_api::Composition;
using spinel_api::Context;
using spinel_api::Interface;
using spinel_api::PathBuilder;
using spinel_api::RasterBuilder;
using spinel_api::Styling;

static Interface * s_interface = nullptr;

Interface *
spinel_api::SetImplementation(Interface * implementation)
{
  Interface * previous = s_interface;
  s_interface          = implementation;
  return previous;
}

// Helper functions to convert from spn_<type>_t values to <Type> pointers.
static Context *
fromSpinel(spn_context_t context)
{
  return reinterpret_cast<Context *>(context);
}

static PathBuilder *
fromSpinel(spn_path_builder_t builder)
{
  return reinterpret_cast<PathBuilder *>(builder);
}

static RasterBuilder *
fromSpinel(spn_raster_builder_t builder)
{
  return reinterpret_cast<RasterBuilder *>(builder);
}

static Composition *
fromSpinel(spn_composition_t composition)
{
  return reinterpret_cast<Composition *>(composition);
}

static Styling *
fromSpinel(spn_styling_t styling)
{
  return reinterpret_cast<Styling *>(styling);
}

extern "C" {

/////////////////////////////////////////////////////////////////////////////
//
//  spinel_assert.c
//

// This is required to implement the spn() macro properly.
extern "C" spn_result_t
spn_assert_1(char const * const file,
             int32_t const      line,
             bool const         is_abort,
             spn_result_t const result)
{
  if (result != SPN_SUCCESS)
    {
      fprintf(stderr, "\"%s\", line %d: spn_assert(%d)\"\n", file, line, result);
      if (is_abort)
        {
          abort();
        }
    }

  return result;
}

/////////////////////////////////////////////////////////////////////////////
//
//  context.c
//

spn_result_t
spn_context_retain(spn_context_t context)
{
  return fromSpinel(context)->retain();
}

spn_result_t
spn_context_release(spn_context_t context)
{
  return fromSpinel(context)->release();
}

spn_result_t
spn_context_reset(spn_context_t context)
{
  return fromSpinel(context)->reset();
}

spn_result_t
spn_context_status(spn_context_t context)
{
  return fromSpinel(context)->status();
}

/////////////////////////////////////////////////////////////////////////////
//
//  path_builder.c
//

spn_result_t
spn_path_builder_create(spn_context_t context, spn_path_builder_t * path_builder)
{
  return fromSpinel(context)->createPathBuilder(path_builder);
}

spn_result_t
spn_path_builder_retain(spn_path_builder_t path_builder)
{
  return fromSpinel(path_builder)->retain();
}

spn_result_t
spn_path_builder_release(spn_path_builder_t path_builder)
{
  return fromSpinel(path_builder)->release();
}

spn_result_t
spn_path_builder_flush(spn_path_builder_t path_builder)
{
  return fromSpinel(path_builder)->flush();
}

spn_result_t
spn_path_builder_begin(spn_path_builder_t path_builder)
{
  return fromSpinel(path_builder)->begin();
}

spn_result_t
spn_path_builder_end(spn_path_builder_t path_builder, spn_path_t * path)
{
  return fromSpinel(path_builder)->end(path);
}

spn_result_t
spn_path_builder_move_to(spn_path_builder_t path_builder, float x0, float y0)
{
  return fromSpinel(path_builder)->moveTo(x0, y0);
}

spn_result_t
spn_path_builder_line_to(spn_path_builder_t path_builder, float x0, float y0)
{
  return fromSpinel(path_builder)->lineTo(x0, y0);
}

spn_result_t
spn_path_builder_quad_to(spn_path_builder_t path_builder, float x0, float y0, float x1, float y1)
{
  return fromSpinel(path_builder)->quadTo(x0, y0, x1, y1);
}

spn_result_t
spn_path_builder_cubic_to(
  spn_path_builder_t path_builder, float x0, float y0, float x1, float y1, float x2, float y2)
{
  return fromSpinel(path_builder)->cubicTo(x0, y0, x1, y1, x2, y2);
}

spn_result_t
spn_path_builder_quad_smooth_to(spn_path_builder_t path_builder,  //
                                float              x2,
                                float              y2)
{
  ASSERT_MSG(false, "Unimplemented");
  return SPN_ERROR_CONTEXT_LOST;
}

spn_result_t
spn_path_builder_cubic_smooth_to(
  spn_path_builder_t path_builder, float x1, float y1, float x2, float y2)
{
  ASSERT_MSG(false, "Unimplemented");
  return SPN_ERROR_CONTEXT_LOST;
}

spn_result_t
spn_path_builder_rat_quad_to(
  spn_path_builder_t path_builder, float x0, float y0, float x1, float y1, float w1)
{
  return fromSpinel(path_builder)->ratQuadTo(x0, y0, x1, y1, w1);
}

spn_result_t
spn_path_builder_rat_cubic_to(spn_path_builder_t path_builder,
                              float              x0,
                              float              y0,
                              float              x1,
                              float              y1,
                              float              x2,
                              float              y2,
                              float              w1,
                              float              w2)
{
  return fromSpinel(path_builder)->ratCubicTo(x0, y0, x1, y1, x2, y2, w1, w2);
}

spn_result_t
spn_path_builder_ellipse(spn_path_builder_t path_builder,  //
                         float              cx,
                         float              cy,
                         float              rx,
                         float              ry)
{
  spn_path_builder_move_to(path_builder, cx, cy + ry);

#define SPN_KAPPA_FLOAT 0.55228474983079339840f  // moar digits!

  float const kx = rx * SPN_KAPPA_FLOAT;
  float const ky = ry * SPN_KAPPA_FLOAT;

  spn_result_t err;
  err = spn_path_builder_cubic_to(path_builder, cx + kx, cy + ry, cx + rx, cy + ky, cx + rx, cy);
  if (err)
    return err;

  err = spn_path_builder_cubic_to(path_builder, cx + rx, cy - ky, cx + kx, cy - ry, cx, cy - ry);
  if (err)
    return err;

  err = spn_path_builder_cubic_to(path_builder, cx - kx, cy - ry, cx - rx, cy - ky, cx - rx, cy);
  if (err)
    return err;

  err = spn_path_builder_cubic_to(path_builder, cx - rx, cy + ky, cx - kx, cy + ry, cx, cy + ry);
  return err;
}

spn_result_t
spn_path_retain(spn_context_t context, spn_path_t const * paths, uint32_t count)
{
  return fromSpinel(context)->retainPaths(paths, count);
}

spn_result_t
spn_path_release(spn_context_t context, spn_path_t const * paths, uint32_t count)
{
  return fromSpinel(context)->releasePaths(paths, count);
}

/////////////////////////////////////////////////////////////////////////////
//
//  raster_builder.c
//

spn_result_t
spn_raster_builder_create(spn_context_t context, spn_raster_builder_t * raster_builder)
{
  return fromSpinel(context)->createRasterBuilder(raster_builder);
}

spn_result_t
spn_raster_builder_retain(spn_raster_builder_t raster_builder)
{
  return fromSpinel(raster_builder)->retain();
}

spn_result_t
spn_raster_builder_release(spn_raster_builder_t raster_builder)
{
  return fromSpinel(raster_builder)->release();
}

spn_result_t
spn_raster_builder_flush(spn_raster_builder_t raster_builder)
{
  return fromSpinel(raster_builder)->flush();
}

//
// RASTER OPS
//

spn_result_t
spn_raster_builder_begin(spn_raster_builder_t raster_builder)
{
  return fromSpinel(raster_builder)->begin();
}

spn_result_t
spn_raster_builder_end(spn_raster_builder_t raster_builder, spn_raster_t * raster)
{
  return fromSpinel(raster_builder)->end(raster);
}

spn_result_t
spn_raster_builder_add(spn_raster_builder_t      raster_builder,
                       spn_path_t const *        paths,
                       spn_transform_weakref_t * transform_weakrefs,
                       spn_transform_t const *   transforms,
                       spn_clip_weakref_t *      clip_weakrefs,
                       spn_clip_t const *        clips,
                       uint32_t                  count)
{
  return fromSpinel(raster_builder)
    ->add(paths, transform_weakrefs, transforms, clip_weakrefs, clips, count);
}

spn_result_t
spn_raster_retain(spn_context_t context, spn_raster_t const * rasters, uint32_t count)
{
  return fromSpinel(context)->retainRasters(rasters, count);
}

spn_result_t
spn_raster_release(spn_context_t context, spn_raster_t const * rasters, uint32_t count)
{
  return fromSpinel(context)->releaseRasters(rasters, count);
}

/////////////////////////////////////////////////////////////////////////////
//
//  composition.c
//

spn_result_t
spn_composition_create(spn_context_t context, spn_composition_t * composition)
{
  return fromSpinel(context)->createComposition(composition);
}

spn_result_t
spn_composition_clone(spn_context_t       context,
                      spn_composition_t   composition,
                      spn_composition_t * clone)
{
  return fromSpinel(context)->cloneComposition(composition, clone);
}

spn_result_t
spn_composition_retain(spn_composition_t composition)
{
  return fromSpinel(composition)->retain();
}

spn_result_t
spn_composition_release(spn_composition_t composition)
{
  return fromSpinel(composition)->release();
}

spn_result_t
spn_composition_place(spn_composition_t    composition,
                      spn_raster_t const * rasters,
                      spn_layer_id const * layer_ids,
                      spn_txty_t const *   txtys,
                      uint32_t             count)
{
  return fromSpinel(composition)->place(rasters, layer_ids, txtys, count);
}

spn_result_t
spn_composition_seal(spn_composition_t composition)
{
  return fromSpinel(composition)->seal();
}

spn_result_t
spn_composition_unseal(spn_composition_t composition)
{
  return fromSpinel(composition)->unseal();
}

spn_result_t
spn_composition_reset(spn_composition_t composition)
{
  return fromSpinel(composition)->reset();
}

spn_result_t
spn_composition_get_bounds(spn_composition_t composition, uint32_t bounds[4])
{
  return fromSpinel(composition)->getBounds(bounds);
}

spn_result_t
spn_composition_set_clip(spn_composition_t composition, uint32_t const clip[4])
{
  return fromSpinel(composition)->setClip(clip);
}

/////////////////////////////////////////////////////////////////////////////
//
//  styling.c
//

spn_result_t
spn_styling_create(spn_context_t   context,
                   spn_styling_t * styling,
                   uint32_t        layers_count,
                   uint32_t        cmds_count)
{
  return fromSpinel(context)->createStyling(layers_count, cmds_count, styling);
}

spn_result_t
spn_styling_retain(spn_styling_t styling)
{
  return fromSpinel(styling)->retain();
}

spn_result_t
spn_styling_release(spn_styling_t styling)
{
  return fromSpinel(styling)->release();
}

spn_result_t
spn_styling_seal(spn_styling_t styling)
{
  return fromSpinel(styling)->seal();
}

spn_result_t
spn_styling_unseal(spn_styling_t styling)
{
  return fromSpinel(styling)->unseal();
}

spn_result_t
spn_styling_reset(spn_styling_t styling)
{
  return fromSpinel(styling)->reset();
}

spn_result_t
spn_styling_group_alloc(spn_styling_t styling, spn_group_id * const group_id)
{
  return fromSpinel(styling)->groupAllocId(group_id);
}

spn_result_t
spn_styling_group_enter(spn_styling_t      styling,
                        spn_group_id const group_id,
                        uint32_t const     n,
                        uint32_t ** const  cmds)
{
  return fromSpinel(styling)->groupAllocEnterCommands(group_id, n, cmds);
}

spn_result_t
spn_styling_group_leave(spn_styling_t      styling,
                        spn_group_id const group_id,
                        uint32_t const     n,
                        uint32_t ** const  cmds)
{
  return fromSpinel(styling)->groupAllocLeaveCommands(group_id, n, cmds);
}

spn_result_t
spn_styling_group_parents(spn_styling_t      styling,
                          spn_group_id const group_id,
                          uint32_t const     n,
                          uint32_t ** const  parents)
{
  return fromSpinel(styling)->groupAllocParents(group_id, n, parents);
}

spn_result_t
spn_styling_group_range_lo(spn_styling_t      styling,
                           spn_group_id const group_id,
                           spn_layer_id const layer_lo)
{
  return fromSpinel(styling)->groupSetRangeLo(group_id, layer_lo);
}

spn_result_t
spn_styling_group_range_hi(spn_styling_t      styling,
                           spn_group_id const group_id,
                           spn_layer_id const layer_hi)
{
  return fromSpinel(styling)->groupSetRangeHi(group_id, layer_hi);
}

spn_result_t
spn_styling_group_layer(spn_styling_t              styling,
                        spn_group_id const         group_id,
                        spn_layer_id const         layer_id,
                        uint32_t const             n,
                        spn_styling_cmd_t ** const cmds)
{
  return fromSpinel(styling)->groupAllocLayerCommands(group_id, layer_id, n, cmds);
}

void
spn_styling_layer_fill_rgba_encoder(spn_styling_cmd_t * cmds, float const rgba[4])
{
  s_interface->encodeCommandFillRgba(cmds, rgba);
}

void
spn_styling_background_over_encoder(spn_styling_cmd_t * cmds, float const rgba[4])
{
  s_interface->encodeCommandBackgroundOver(cmds, rgba);
}

/////////////////////////////////////////////////////////////////////////////
//
//  render.c
//

spn_result_t
spn_render(spn_context_t context, const spn_render_submit_t * submit)
{
  return fromSpinel(context)->render(submit);
}

}  // extern "C"
