// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_COMPOSITION_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_COMPOSITION_H_

//
//
//

#include "spinel/spinel.h"

//
//
//

struct spn_composition
{
  struct spn_context *          context;
  struct spn_composition_impl * impl;

  // clang-format off
  spn_result_t (* release   )(struct spn_composition_impl * const impl);

  spn_result_t (* place     )(struct spn_composition_impl * const impl,
                            spn_raster_t const *                rasters,
                            spn_layer_id const *                layer_ids,
                            spn_txty_t const *                  txtys,
                            uint32_t                            count);

  spn_result_t (* seal      )(struct spn_composition_impl * const impl);
  spn_result_t (* unseal    )(struct spn_composition_impl * const impl);
  spn_result_t (* reset     )(struct spn_composition_impl * const impl);
  spn_result_t (* clone     )(struct spn_composition_impl * const impl, struct spn_composition * * const clone);
  spn_result_t (* get_bounds)(struct spn_composition_impl * const impl, uint32_t bounds[4]);
  spn_result_t (* set_clip  )(struct spn_composition_impl * const impl, uint32_t const clip[4]);
  // clang-format on

  int32_t ref_count;
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_COMPOSITION_H_
