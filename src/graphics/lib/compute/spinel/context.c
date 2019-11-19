// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "context.h"

#include <stdlib.h>

#include "spinel/spinel.h"

//
//
//

spn_result_t
spn_context_status(spn_context_t context)
{
  return context->status(context->device);
}

//
//
//

spn_result_t
spn_context_retain(spn_context_t context)
{
  return SPN_SUCCESS;
}

spn_result_t
spn_context_release(spn_context_t context)
{
  return context->dispose(context->device);
}

//
//
//

spn_result_t
spn_path_builder_create(spn_context_t context, spn_path_builder_t * path_builder)
{
  return context->path_builder(context->device, path_builder);
}

spn_result_t
spn_path_retain(spn_context_t context, spn_path_t const * paths, uint32_t count)
{
  return context->path_retain(context->device, paths, count);
}

spn_result_t
spn_path_release(spn_context_t context, spn_path_t const * paths, uint32_t count)
{
  return context->path_release(context->device, paths, count);
}

//
//
//

spn_result_t
spn_raster_builder_create(spn_context_t context, spn_raster_builder_t * raster_builder)
{
  return context->raster_builder(context->device, raster_builder);
}

spn_result_t
spn_raster_retain(spn_context_t context, spn_raster_t const * rasters, uint32_t count)
{
  return context->raster_retain(context->device, rasters, count);
}

spn_result_t
spn_raster_release(spn_context_t context, spn_raster_t const * rasters, uint32_t count)
{
  return context->raster_release(context->device, rasters, count);
}

//
//
//

spn_result_t
spn_styling_create(spn_context_t   context,
                   spn_styling_t * styling,
                   uint32_t        layers_count,
                   uint32_t        cmds_count)
{
  return context->styling(context->device, styling, layers_count, cmds_count);
}

//
//
//

spn_result_t
spn_composition_create(spn_context_t context, spn_composition_t * composition)
{
  return context->composition(context->device, composition);
}

//
//
//

spn_result_t
spn_render(spn_context_t context, spn_render_submit_t const * const submit)
{
  return context->render(context->device, submit);
}

//
//
//
