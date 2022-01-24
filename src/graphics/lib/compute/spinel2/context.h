// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_CONTEXT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_CONTEXT_H_

//
//
//

#include "spinel/spinel_result.h"
#include "spinel/spinel_types.h"

//
//
//

struct spinel_context
{
  //
  //
  //
  struct spinel_device * device;

  //
  //
  //
  spinel_result_t (*dispose)(struct spinel_device * device);

  //
  //
  //
  spinel_result_t (*get_limits)(struct spinel_device *    device,  //
                                spinel_context_limits_t * limits);

  //
  //
  //
  spinel_result_t (*path_builder)(struct spinel_device *  device,
                                  spinel_path_builder_t * path_builder);

  spinel_result_t (*path_retain)(struct spinel_device * device,
                                 spinel_path_t const *  paths,
                                 uint32_t               count);

  spinel_result_t (*path_release)(struct spinel_device * device,
                                  spinel_path_t const *  paths,
                                  uint32_t               count);

  //
  //
  //
  spinel_result_t (*raster_builder)(struct spinel_device *    device,
                                    spinel_raster_builder_t * raster_builder);

  spinel_result_t (*raster_retain)(struct spinel_device *  device,
                                   spinel_raster_t const * rasters,
                                   uint32_t                count);

  spinel_result_t (*raster_release)(struct spinel_device *  device,
                                    spinel_raster_t const * rasters,
                                    uint32_t                count);

  //
  //
  //
  spinel_result_t (*composition)(struct spinel_device * device,  //
                                 spinel_composition_t * composition);

  //
  //
  //
  spinel_result_t (*styling)(struct spinel_device *               device,
                             spinel_styling_create_info_t const * create_info,
                             spinel_styling_t *                   styling);

  //
  //
  //
  spinel_result_t (*swapchain)(struct spinel_device *                 device,  //
                               spinel_swapchain_create_info_t const * create_info,
                               spinel_swapchain_t *                   swapchain);

  //
  //
  //
  int32_t refcount;
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_CONTEXT_H_
