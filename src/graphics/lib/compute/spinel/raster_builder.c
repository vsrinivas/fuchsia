// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//


#include "raster_builder.h"
#include "float.h"

//
// IDENTITY TRANSFORM
//

float const spn_transform_identity[8] =
  {
    1.0f, 0.0f, // sx  shx
    0.0f, 1.0f, // shy sy
    0.0f, 0.0f, // tx  ty
    0.0f, 0.0f  // w0  w1
  };

//
// DEFAULT CLIP
//

float const spn_clip_default[4] =
  {
    -FLT_MAX, -FLT_MAX, // lower left  corner of bounding box
    +FLT_MAX, +FLT_MAX  // upper right corner of bounding box
  };

//
//
//

spn_result
spn_raster_builder_retain(spn_raster_builder_t raster_builder)
{
  raster_builder->refcount += 1;

  return SPN_SUCCESS;
}

spn_result
spn_raster_builder_release(spn_raster_builder_t raster_builder)
{
  SPN_ASSERT_STATE_ASSERT(SPN_RASTER_BUILDER_STATE_READY,raster_builder);

  raster_builder->release(raster_builder->impl);

  return SPN_SUCCESS;
}

//
//
//

spn_result
spn_raster_begin(spn_raster_builder_t raster_builder)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_RASTER_BUILDER_STATE_READY,
                              SPN_RASTER_BUILDER_STATE_BUILDING,
                              raster_builder);

  return raster_builder->begin(raster_builder->impl);
}

spn_result
spn_raster_end(spn_raster_builder_t raster_builder, spn_raster_t * raster)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_RASTER_BUILDER_STATE_BUILDING,
                              SPN_RASTER_BUILDER_STATE_READY,
                              raster_builder);

  return raster_builder->end(raster_builder->impl,raster);
}

//
//
//

spn_result
spn_raster_flush(spn_raster_builder_t raster_builder)
{
  //
  // it doesn't matter what the state is
  //
  return raster_builder->flush(raster_builder->impl);
}

//
//
//

spn_result
spn_raster_fill(spn_raster_builder_t         raster_builder,
                spn_path_t                 * paths,
                spn_transform_weakref_t    * transform_weakrefs,
                float               const (* transforms)[8],
                spn_clip_weakref_t         * clip_weakrefs,
                float               const (* clips)[4],
                uint32_t                     count)
{
  SPN_ASSERT_STATE_ASSERT(SPN_RASTER_BUILDER_STATE_BUILDING,
                          raster_builder);

  return raster_builder->fill(raster_builder->impl,
                              paths,
                              transform_weakrefs,
                              transforms,
                              clip_weakrefs,
                              clips,
                              count);
}

//
//
//
