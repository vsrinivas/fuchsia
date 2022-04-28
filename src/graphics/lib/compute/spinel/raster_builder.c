// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "raster_builder.h"

//
//
//

spinel_result_t
spinel_raster_builder_retain(spinel_raster_builder_t raster_builder)
{
  assert(raster_builder->ref_count >= 1);

  ++raster_builder->ref_count;

  return SPN_SUCCESS;
}

spinel_result_t
spinel_raster_builder_release(spinel_raster_builder_t raster_builder)
{
  assert(raster_builder->ref_count >= 1);

  SPN_ASSERT_STATE_ASSERT(SPN_RASTER_BUILDER_STATE_READY, raster_builder);

  if (--raster_builder->ref_count == 0)
    {
      return raster_builder->release(raster_builder->impl);
    }
  else
    {
      return SPN_SUCCESS;
    }
}

//
//
//

spinel_result_t
spinel_raster_builder_begin(spinel_raster_builder_t raster_builder)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_RASTER_BUILDER_STATE_READY,
                              SPN_RASTER_BUILDER_STATE_BUILDING,
                              raster_builder);

  return raster_builder->begin(raster_builder->impl);
}

spinel_result_t
spinel_raster_builder_end(spinel_raster_builder_t raster_builder, spinel_raster_t * raster)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_RASTER_BUILDER_STATE_BUILDING,
                              SPN_RASTER_BUILDER_STATE_READY,
                              raster_builder);

  return raster_builder->end(raster_builder->impl, raster);
}

//
//
//

spinel_result_t
spinel_raster_builder_flush(spinel_raster_builder_t raster_builder)
{
  //
  // it doesn't matter what the state is
  //
  return raster_builder->flush(raster_builder->impl);
}

//
//
//

spinel_result_t
spinel_raster_builder_add(spinel_raster_builder_t      raster_builder,
                          spinel_path_t const *        paths,
                          spinel_transform_weakref_t * transform_weakrefs,
                          spinel_transform_t const *   transforms,
                          spinel_clip_weakref_t *      clip_weakrefs,
                          spinel_clip_t const *        clips,
                          uint32_t                     count)
{
  SPN_ASSERT_STATE_ASSERT(SPN_RASTER_BUILDER_STATE_BUILDING, raster_builder);

  return raster_builder->add(raster_builder->impl,  //
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
