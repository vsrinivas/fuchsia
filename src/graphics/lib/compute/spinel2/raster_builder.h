// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_RASTER_BUILDER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_RASTER_BUILDER_H_

//
//
//

#include "spinel/spinel.h"
#include "state_assert.h"

//
//
//

typedef enum spinel_raster_builder_state_e
{
  SPN_RASTER_BUILDER_STATE_READY,
  SPN_RASTER_BUILDER_STATE_BUILDING
} spinel_raster_builder_state_e;

//
// Construct and dispose of a raster builder and its opaque
// implementation.
//

struct spinel_raster_builder
{
  struct spinel_raster_builder_impl * impl;

  // clang-format off
  spinel_result_t (* begin  )(struct spinel_raster_builder_impl * impl);
  spinel_result_t (* end    )(struct spinel_raster_builder_impl * impl, spinel_raster_t * raster);
  spinel_result_t (* release)(struct spinel_raster_builder_impl * impl);
  spinel_result_t (* flush  )(struct spinel_raster_builder_impl * impl);
  spinel_result_t (* add    )(struct spinel_raster_builder_impl * impl,
                              spinel_path_t const *               paths,
                              spinel_transform_weakref_t *        transform_weakrefs,
                              spinel_transform_t const *          transforms,
                              spinel_clip_weakref_t *             clip_weakrefs,
                              spinel_clip_t const *               clips,
                              uint32_t                            count);
  // clang-format on

  int32_t ref_count;

  SPN_ASSERT_STATE_DECLARE(spinel_raster_builder_state_e);
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_RASTER_BUILDER_H_
