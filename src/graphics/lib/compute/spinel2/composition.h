// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_COMPOSITION_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_COMPOSITION_H_

//
//
//

#include "spinel/spinel.h"

//
//
//

struct spinel_composition
{
  struct spinel_context *          context;
  struct spinel_composition_impl * impl;

  // clang-format off
  spinel_result_t (* release   )(struct spinel_composition_impl * impl);

  spinel_result_t (* place     )(struct spinel_composition_impl * impl,
                                 spinel_raster_t const *          rasters,
                                 spinel_layer_id const *          layer_ids,
                                 spinel_txty_t   const *          txtys,
                                 uint32_t                         count);

  spinel_result_t (* seal      )(struct spinel_composition_impl * impl);
  spinel_result_t (* unseal    )(struct spinel_composition_impl * impl);
  spinel_result_t (* reset     )(struct spinel_composition_impl * impl);
  spinel_result_t (* set_clip  )(struct spinel_composition_impl * impl, spinel_pixel_clip_t const * clip);
  // clang-format on

  int32_t ref_count;
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_COMPOSITION_H_
