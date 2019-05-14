// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CONTEXT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CONTEXT_H_

//
//
//

#include "spinel_result.h"
#include "spinel_types.h"

//
//
//

struct spn_context
{
  struct spn_device * device;

  //
  //
  //

  spn_result (*dispose)(struct spn_device * const device);

  spn_result (*reset)(struct spn_device * const device);

  spn_result (*yield)(struct spn_device * const device);

  spn_result (*wait)(struct spn_device * const device);

  //
  //
  //

  spn_result (*path_builder)(struct spn_device * const  device,
                             spn_path_builder_t * const path_builder);

  spn_result (*path_retain)(struct spn_device * const device,
                            spn_path_t const *        paths,
                            uint32_t                  count);

  spn_result (*path_release)(struct spn_device * const device,
                             spn_path_t const *        paths,
                             uint32_t                  count);

  //
  //
  //

  spn_result (*raster_builder)(struct spn_device * const    device,
                               spn_raster_builder_t * const raster_builder);

  spn_result (*raster_retain)(struct spn_device * const device,
                              spn_raster_t const *      rasters,
                              uint32_t                  count);

  spn_result (*raster_release)(struct spn_device * const device,
                               spn_raster_t const *      rasters,
                               uint32_t                  count);

  //
  //
  //

  spn_result (*composition)(struct spn_device * const device,
                            spn_composition_t * const composition);

  //
  //
  //

  spn_result (*styling)(struct spn_device * const device,
                        spn_styling_t * const     styling,
                        uint32_t const            layers_count,
                        uint32_t const            dwords_count);

  //
  //
  //

  spn_result (*render)(struct spn_device * const device, spn_render_submit_t const * const submit);

  //
  //
  //

  int32_t refcount;
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CONTEXT_H_
