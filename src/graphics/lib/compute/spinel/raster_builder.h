// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_RASTER_BUILDER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_RASTER_BUILDER_H_

//
//
//

#include "spinel/spinel.h"
#include "state_assert.h"

//
//
//

typedef enum spn_raster_builder_state_e
{
  SPN_RASTER_BUILDER_STATE_READY,
  SPN_RASTER_BUILDER_STATE_BUILDING
} spn_raster_builder_state_e;

//
// Construct and dispose of a raster builder and its opaque
// implementation.
//

struct spn_raster_builder
{
  struct spn_raster_builder_impl * impl;

  //
  // clang-format off
  //
  spn_result_t (* begin  )(struct spn_raster_builder_impl * const impl);
  spn_result_t (* end    )(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster);
  spn_result_t (* release)(struct spn_raster_builder_impl * const impl);
  spn_result_t (* flush  )(struct spn_raster_builder_impl * const impl);
  spn_result_t (* add    )(struct spn_raster_builder_impl * const impl,
                           spn_path_t const *        paths,
                           spn_transform_weakref_t * transform_weakrefs,
                           spn_transform_t const *   transforms,
                           spn_clip_weakref_t *      clip_weakrefs,
                           spn_clip_t const *        clips,
                           uint32_t                  count);

  //
  // clang-format on
  //

  int32_t refcount;

  SPN_ASSERT_STATE_DECLARE(spn_raster_builder_state_e);
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_RASTER_BUILDER_H_
