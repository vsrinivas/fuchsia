// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdlib.h>
#include <assert.h> // FIXME -- replace with an SPN assert for non-debug builds

#include "context.h"
#include "spinel.h"

//
// CONTEXT
//

#if 0

//
// FIXME -- we're going to use a Vulkan-style idiom to simply context
// creation
//

spn_result
spn_context_create_cl(spn_context_t * context,
                      cl_context      context_cl,
                      cl_device_id    device_id_cl)
{
  (*context) = malloc(sizeof(**context));

  //
  // FIXME -- we'll clean up context creation by platform later.  For
  // now, just create a CL_12 context.
  //
  spn_result err;

  err = spn_platform_cl_12_create(*context,context_cl,device_id_cl);

  return err;
}

#endif

//
//
//

spn_result
spn_context_retain(spn_context_t context)
{
  return SPN_SUCCESS;
}


spn_result
spn_context_release(spn_context_t context)
{
  return context->dispose(context->device);
}

spn_result
spn_context_reset(spn_context_t context)
{
  return SPN_SUCCESS;
}

spn_result
spn_context_yield(spn_context_t context)
{
  return context->yield(context->device);
}

spn_result
spn_context_wait(spn_context_t context)
{
  return context->wait(context->device);
}

//
//
//

spn_result
spn_path_builder_create(spn_context_t context, spn_path_builder_t * path_builder)
{
  return context->path_builder(context->device,path_builder);
}

spn_result
spn_path_retain(spn_context_t context, spn_path_t const * paths, uint32_t count)
{
  return context->path_retain(context->device,paths,count);
}

spn_result
spn_path_release(spn_context_t context, spn_path_t const * paths, uint32_t count)
{
  return context->path_release(context->device,paths,count);
}

//
//
//

spn_result
spn_raster_builder_create(spn_context_t context, spn_raster_builder_t * raster_builder)
{
  return context->raster_builder(context->device,raster_builder);
}

spn_result
spn_raster_retain(spn_context_t context, spn_raster_t const * rasters, uint32_t count)
{
  return context->raster_retain(context->device,rasters,count);
}

spn_result
spn_raster_release(spn_context_t context, spn_raster_t const * rasters, uint32_t count)
{
  return context->raster_release(context->device,rasters,count);
}

//
//
//

spn_result
spn_styling_create(spn_context_t   context,
                   spn_styling_t * styling,
                   uint32_t        layers_count,
                   uint32_t        dwords_count)
{
  return context->styling(context->device,
                          styling,
                          layers_count,
                          dwords_count);
}

//
//
//

spn_result
spn_composition_create(spn_context_t context, spn_composition_t * composition)
{
  return context->composition(context->device,composition);
}

//
//
//

spn_result
spn_render(spn_context_t context, spn_render_submit_t const * const submit)
{
  return context->render(context->device,submit);
}

//
//
//
