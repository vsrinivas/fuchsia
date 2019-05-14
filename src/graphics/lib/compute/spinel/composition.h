// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_COMPOSITION_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_COMPOSITION_H_

//
//
//

#include "spinel.h"

//
//
//

struct spn_composition
{
  struct spn_context *          context;
  struct spn_composition_impl * impl;

  //
  // clang-format off
  //
  spn_result (* release   )(struct spn_composition_impl * const impl);
  spn_result (* seal      )(struct spn_composition_impl * const impl);
  spn_result (* unseal    )(struct spn_composition_impl * const impl);
  spn_result (* reset     )(struct spn_composition_impl * const impl);
  spn_result (* clone     )(struct spn_composition_impl * const impl, struct spn_composition * * const clone);
  spn_result (* get_bounds)(struct spn_composition_impl * const impl, int32_t bounds[4]);
  spn_result (* place     )(struct spn_composition_impl * const impl,
                                   spn_raster_t  const  *       rasters,
                                   spn_layer_id  const  *       layer_ids,
                                   int32_t       const (*       txtys)[2],
                                   uint32_t                     count);
  //
  // clang-format on
  //

  int32_t ref_count;
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_COMPOSITION_H_
