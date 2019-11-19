// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CONTEXT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CONTEXT_H_

//
//
//

#include "spinel/spinel_result.h"
#include "spinel/spinel_types.h"

//
//
//

struct spn_context
{
  struct spn_device * device;

  //
  //
  //
  spn_result_t (*status)(struct spn_device * const device);

  //
  //
  //

  spn_result_t (*dispose)(struct spn_device * const device);

  //
  //
  //

  spn_result_t (*path_builder)(struct spn_device * const  device,
                               spn_path_builder_t * const path_builder);

  spn_result_t (*path_retain)(struct spn_device * const device,
                              spn_path_t const *        paths,
                              uint32_t                  count);

  spn_result_t (*path_release)(struct spn_device * const device,
                               spn_path_t const *        paths,
                               uint32_t                  count);

  //
  //
  //

  spn_result_t (*raster_builder)(struct spn_device * const    device,
                                 spn_raster_builder_t * const raster_builder);

  spn_result_t (*raster_retain)(struct spn_device * const device,
                                spn_raster_t const *      rasters,
                                uint32_t                  count);

  spn_result_t (*raster_release)(struct spn_device * const device,
                                 spn_raster_t const *      rasters,
                                 uint32_t                  count);

  //
  //
  //

  spn_result_t (*composition)(struct spn_device * const device,
                              spn_composition_t * const composition);

  //
  //
  //

  spn_result_t (*styling)(struct spn_device * const device,
                          spn_styling_t * const     styling,
                          uint32_t const            layers_count,
                          uint32_t const            cmds_count);

  //
  //
  //

  spn_result_t (*render)(struct spn_device * const         device,
                         spn_render_submit_t const * const submit);

  //
  //
  //

  int32_t refcount;
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CONTEXT_H_
