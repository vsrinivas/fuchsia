// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_BLOCK_POOL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_BLOCK_POOL_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "vk.h"

//
//
//

struct spn_device;

//
// FIXME(allanmac): these are DEBUG ONLY and should be removed after bringup is complete
//

void
spn_device_block_pool_debug_snap(struct spn_device * const device, VkCommandBuffer cb);

void
spn_device_block_pool_debug_print(struct spn_device * const device);

//
//
//

void
spn_device_block_pool_create(struct spn_device * const device,
                             uint64_t const            block_pool_size,
                             uint32_t const            handle_count);

void
spn_device_block_pool_dispose(struct spn_device * const device);

//
//
//

uint32_t
spn_device_block_pool_get_mask(struct spn_device * const device);

//
//
//

struct spn_vk_ds_block_pool_t
spn_device_block_pool_get_ds(struct spn_device * const device);

//
// Returns the number of dwords in the block pool
//

uint32_t
spn_device_block_pool_get_size(struct spn_device * const device);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_BLOCK_POOL_H_
