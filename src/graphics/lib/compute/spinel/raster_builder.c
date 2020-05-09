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

spn_result_t
spn_raster_builder_retain(spn_raster_builder_t raster_builder)
{
  raster_builder->refcount += 1;

  return SPN_SUCCESS;
}

spn_result_t
spn_raster_builder_release(spn_raster_builder_t raster_builder)
{
  SPN_ASSERT_STATE_ASSERT(SPN_RASTER_BUILDER_STATE_READY, raster_builder);

  raster_builder->release(raster_builder->impl);

  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_raster_builder_begin(spn_raster_builder_t raster_builder)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_RASTER_BUILDER_STATE_READY,
                              SPN_RASTER_BUILDER_STATE_BUILDING,
                              raster_builder);

  return raster_builder->begin(raster_builder->impl);
}

spn_result_t
spn_raster_builder_end(spn_raster_builder_t raster_builder, spn_raster_t * raster)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_RASTER_BUILDER_STATE_BUILDING,
                              SPN_RASTER_BUILDER_STATE_READY,
                              raster_builder);

  return raster_builder->end(raster_builder->impl, raster);
}

//
//
//

spn_result_t
spn_raster_builder_flush(spn_raster_builder_t raster_builder)
{
  //
  // it doesn't matter what the state is
  //
  return raster_builder->flush(raster_builder->impl);
}

//
//
//

spn_result_t
spn_raster_builder_add(spn_raster_builder_t      raster_builder,
                       spn_path_t const *        paths,
                       spn_transform_weakref_t * transform_weakrefs,
                       spn_transform_t const *   transforms,
                       spn_clip_weakref_t *      clip_weakrefs,
                       spn_clip_t const *        clips,
                       uint32_t                  count)
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
