// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include "spinel.h"
#include "state_assert.h"

//
//
//

typedef enum spn_raster_builder_state_e {

  SPN_RASTER_BUILDER_STATE_READY,
  SPN_RASTER_BUILDER_STATE_BUILDING

} spn_raster_builder_state_e;

//
// Construct and dispose of a raster builder and its opaque
// implementation.
//

struct spn_raster_builder
{
  struct spn_raster_builder_impl  * impl;

  spn_result                     (* begin  )(struct spn_raster_builder_impl * const impl);
  spn_result                     (* end    )(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster);
  spn_result                     (* release)(struct spn_raster_builder_impl * const impl);
  spn_result                     (* flush  )(struct spn_raster_builder_impl * const impl);
  spn_result                     (* fill   )(struct spn_raster_builder_impl * const impl,
                                             spn_path_t                     * const paths,
                                             spn_transform_weakref_t        * const transform_weakrefs,
                                             float                   const (* const transforms)[8],
                                             spn_clip_weakref_t             * const clip_weakrefs,
                                             float                   const (* const clips)[4],
                                             uint32_t                               count);

  int32_t                           refcount;

  SPN_ASSERT_STATE_DECLARE(spn_raster_builder_state_e);
};

//
//
//
