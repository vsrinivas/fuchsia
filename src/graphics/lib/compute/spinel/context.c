// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "context.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "spinel/spinel.h"

//
//
//
spinel_result_t
spinel_context_retain(spinel_context_t context)
{
  assert(context->refcount >= 1);
  assert(context->refcount < INT32_MAX);

  context->refcount += 1;

  return SPN_SUCCESS;
}

spinel_result_t
spinel_context_release(spinel_context_t context)
{
  assert(context->refcount >= 1);

  context->refcount -= 1;

  if (context->refcount == 0)
    {
      return context->dispose(context->device);
    }

  return SPN_SUCCESS;
}

//
//
//
spinel_result_t
spinel_context_get_limits(spinel_context_t context, spinel_context_limits_t * limits)
{
  return context->get_limits(context->device, limits);
}

//
//
//
spinel_result_t
spinel_path_builder_create(spinel_context_t context, spinel_path_builder_t * path_builder)
{
  return context->path_builder(context->device, path_builder);
}

spinel_result_t
spinel_path_retain(spinel_context_t context, spinel_path_t const * paths, uint32_t count)
{
  return context->path_retain(context->device, paths, count);
}

spinel_result_t
spinel_path_release(spinel_context_t context, spinel_path_t const * paths, uint32_t count)
{
  return context->path_release(context->device, paths, count);
}

//
//
//
spinel_result_t
spinel_raster_builder_create(spinel_context_t context, spinel_raster_builder_t * raster_builder)
{
  return context->raster_builder(context->device, raster_builder);
}

spinel_result_t
spinel_raster_retain(spinel_context_t context, spinel_raster_t const * rasters, uint32_t count)
{
  return context->raster_retain(context->device, rasters, count);
}

spinel_result_t
spinel_raster_release(spinel_context_t context, spinel_raster_t const * rasters, uint32_t count)
{
  return context->raster_release(context->device, rasters, count);
}

//
//
//
spinel_result_t
spinel_composition_create(spinel_context_t context, spinel_composition_t * composition)
{
  return context->composition(context->device, composition);
}

//
//
//
spinel_result_t
spinel_styling_create(spinel_context_t                     context,
                      spinel_styling_create_info_t const * create_info,
                      spinel_styling_t *                   styling)
{
  return context->styling(context->device, create_info, styling);
}

//
//
//
spinel_result_t
spinel_swapchain_create(spinel_context_t                       context,
                        spinel_swapchain_create_info_t const * create_info,
                        spinel_swapchain_t *                   swapchain)
{
  return context->swapchain(context->device, create_info, swapchain);
}

//
//
//
