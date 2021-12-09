// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_HANDLE_POOL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_HANDLE_POOL_H_

//
//
//

#include <stdint.h>

#include "device.h"
#include "spinel/spinel_result.h"
#include "spinel/spinel_types.h"

//
// Unlike other device substructs, keep the handle pool's structures opaque.
//
struct spinel_handle_pool;

//
// Create/dispose the handle pool
//
void
spinel_device_handle_pool_create(struct spinel_device * device, uint32_t handle_count);

void
spinel_device_handle_pool_dispose(struct spinel_device * device);

//
// How many handles in the pool?  This number may differ from the count
// provided at handle pool creation time.
//
uint32_t
spinel_handle_pool_get_handle_count(struct spinel_handle_pool const * handle_pool);

//
// Acquire a handle
//
spinel_handle_t
spinel_device_handle_acquire(struct spinel_device * device);

//
// Host-invoked handle retain
//
spinel_result_t
spinel_device_validate_retain_h_paths(struct spinel_device *     device,
                                      struct spinel_path const * paths,
                                      uint32_t                   count);

spinel_result_t
spinel_device_validate_retain_h_rasters(struct spinel_device *       device,
                                        struct spinel_raster const * rasters,
                                        uint32_t                     count);

//
// Host-invoked handle release
//
spinel_result_t
spinel_device_validate_release_h_paths(struct spinel_device *     device,
                                       struct spinel_path const * paths,
                                       uint32_t                   count);

spinel_result_t
spinel_device_validate_release_h_rasters(struct spinel_device *       device,
                                         struct spinel_raster const * rasters,
                                         uint32_t                     count);

//
// Validate host-provided handles before retaining on the device
//
spinel_result_t
spinel_device_validate_d_paths(struct spinel_device *     device,
                               struct spinel_path const * paths,
                               uint32_t                   count);

spinel_result_t
spinel_device_validate_d_rasters(struct spinel_device *       device,
                                 struct spinel_raster const * rasters,
                                 uint32_t                     count);

//
// After device-side validation, retain the handles for the device
//
void
spinel_device_retain_d_paths(struct spinel_device *     device,
                             struct spinel_path const * paths,
                             uint32_t                   count);

void
spinel_device_retain_d_rasters(struct spinel_device *       device,
                               struct spinel_raster const * rasters,
                               uint32_t                     count);

//
// Release device-held spans of handles of known type
//
void
spinel_device_release_d_paths(struct spinel_device *  device,
                              spinel_handle_t const * handles,
                              uint32_t                count);

void
spinel_device_release_d_rasters(struct spinel_device *  device,
                                spinel_handle_t const * handles,
                                uint32_t                count);

//
// Release handles on a ring -- up to two spans
//
void
spinel_device_release_d_paths_ring(struct spinel_device *  device,
                                   spinel_handle_t const * paths,
                                   uint32_t                size,
                                   uint32_t                head,
                                   uint32_t                span);

void
spinel_device_release_d_rasters_ring(struct spinel_device *  device,
                                     spinel_handle_t const * rasters,
                                     uint32_t                size,
                                     uint32_t                head,
                                     uint32_t                span);
//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_HANDLE_POOL_H_
