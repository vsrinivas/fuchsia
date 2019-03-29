// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include "handle.h"
#include "spinel_result.h"

//
//
//

struct spn_device;

//
//
//

void
spn_device_handle_pool_create(struct spn_device * const device,
                              uint32_t            const handle_count);

void
spn_device_handle_pool_dispose(struct spn_device * const device);

//
//
//

void
spn_device_handle_pool_acquire(struct spn_device * const device,
                               spn_handle_t      * const handle);

//
//
//

spn_result
spn_device_handle_pool_validate_retain_h_paths(struct spn_device * const device,
                                               spn_path_t  const * const typed_handles,
                                               uint32_t            const count);

spn_result
spn_device_handle_pool_validate_retain_h_rasters(struct spn_device * const device,
                                                 spn_path_t  const * const typed_handles,
                                                 uint32_t            const count);

//
//
//

spn_result
spn_device_handle_pool_validate_release_h_paths(struct spn_device * const device,
                                                spn_path_t  const * const typed_handles,
                                                uint32_t            const count);

spn_result
spn_device_handle_pool_validate_release_h_rasters(struct spn_device  * const device,
                                                  spn_raster_t const * const typed_handles,
                                                  uint32_t             const count);

//
//
//

void
spn_device_handle_pool_retain_d(struct spn_device        * const device,
                                spn_typed_handle_t const * const typed_handles,
                                uint32_t                   const count);

spn_result
spn_device_handle_pool_validate_retain_d(struct spn_device        * const device,
                                         spn_typed_handle_type_e    const handle_type,
                                         spn_typed_handle_t const * const typed_handles,
                                         uint32_t                   const count);

//
//
//

void
spn_device_handle_pool_release_d_paths(struct spn_device * const device,
                                       spn_path_t  const * const handles,
                                       uint32_t            const count);

void
spn_device_handle_pool_release_d_rasters(struct spn_device  * const device,
                                         spn_raster_t const * const handles,
                                         uint32_t             const count);

//
//
//
