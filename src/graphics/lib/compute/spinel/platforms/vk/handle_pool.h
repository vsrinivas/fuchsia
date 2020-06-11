// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_HANDLE_POOL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_HANDLE_POOL_H_

//
//
//

#include <stdint.h>

#include "spinel_result.h"

//
//
//

struct spn_path;
struct spn_raster;
struct spn_device;

//
// Handles are ~27-bit indices
//

typedef uint32_t spn_handle_t;

//
// Create/dispose the handle pool
//

void
spn_device_handle_pool_create(struct spn_device * const device, uint32_t const handle_count);

void
spn_device_handle_pool_dispose(struct spn_device * const device);

//
// How many handles in the pool?  This number may differ from the count
// provided at creation time.
//

uint32_t
spn_device_handle_pool_get_handle_count(struct spn_device * const device);

//
// Acquire a handle
//

void
spn_device_handle_pool_acquire(struct spn_device * const device, spn_handle_t * const p_handle);

//
// Host-invoked handle retain
//

spn_result_t
spn_device_handle_pool_validate_retain_h_paths(struct spn_device * const     device,
                                               struct spn_path const * const paths,
                                               uint32_t                      count);

spn_result_t
spn_device_handle_pool_validate_retain_h_rasters(struct spn_device * const       device,
                                                 struct spn_raster const * const rasters,
                                                 uint32_t                        count);

//
// Host-invoked handle release
//

spn_result_t
spn_device_handle_pool_validate_release_h_paths(struct spn_device * const     device,
                                                struct spn_path const * const paths,
                                                uint32_t const                count);

spn_result_t
spn_device_handle_pool_validate_release_h_rasters(struct spn_device * const       device,
                                                  struct spn_raster const * const rasters,
                                                  uint32_t const                  count);

//
// Validate host-provided handles before retaining on the device
//

spn_result_t
spn_device_handle_pool_validate_d_paths(struct spn_device * const     device,
                                        struct spn_path const * const paths,
                                        uint32_t const                count);

spn_result_t
spn_device_handle_pool_validate_d_rasters(struct spn_device * const       device,
                                          struct spn_raster const * const rasters,
                                          uint32_t const                  count);

//
// After device-side validation, retain the handles for the device
//

void
spn_device_handle_pool_retain_d_paths(struct spn_device * const     device,
                                      struct spn_path const * const paths,
                                      uint32_t const                count);

void
spn_device_handle_pool_retain_d_rasters(struct spn_device * const       device,
                                        struct spn_raster const * const rasters,
                                        uint32_t const                  count);

//
// Release device-held spans of handles of known type
//

void
spn_device_handle_pool_release_d_paths(struct spn_device * const  device,
                                       spn_handle_t const * const handles,
                                       uint32_t const             count);

void
spn_device_handle_pool_release_d_rasters(struct spn_device * const  device,
                                         spn_handle_t const * const handles,
                                         uint32_t const             count);

//
// Release handles on a ring -- up to two spans
//

void
spn_device_handle_pool_release_ring_d_paths(struct spn_device * const  device,
                                            spn_handle_t const * const paths,
                                            uint32_t const             size,
                                            uint32_t const             head,
                                            uint32_t const             span);

void
spn_device_handle_pool_release_ring_d_rasters(struct spn_device * const  device,
                                              spn_handle_t const * const rasters,
                                              uint32_t const             size,
                                              uint32_t const             head,
                                              uint32_t const             span);
//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_HANDLE_POOL_H_
