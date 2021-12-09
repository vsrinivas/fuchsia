// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_BLOCK_POOL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_BLOCK_POOL_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "allocator.h"

//
//
//
struct spinel_block_pool
{
  uint32_t bp_size;
  uint32_t bp_mask;

  struct
  {
    struct
    {
      struct spinel_dbi_dm bp;
    } dbi_dm;

    struct
    {
      struct spinel_dbi_devaddr blocks;
      struct spinel_dbi_devaddr ids;
      struct spinel_dbi_devaddr host_map;
    } dbi_devaddr;
  } vk;
};

//
// The `block_pool_size` is in bytes.
//
void
spinel_device_block_pool_create(struct spinel_device * device,
                                uint64_t               block_pool_size,
                                uint32_t               handle_count);

//
//
//
void
spinel_device_block_pool_dispose(struct spinel_device * device);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_BLOCK_POOL_H_
