// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_QUEUE_POOL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_QUEUE_POOL_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel/platforms/vk/spinel_vk_types.h"

//
//
//
#define SPN_QUEUE_POOL_MAX_QUEUES 32

//
//
//
struct spinel_queue_pool
{
  spinel_vk_context_create_info_vk_queue_t create_info;
  uint32_t                                 queue_next;
  VkQueue                                  queues[SPN_QUEUE_POOL_MAX_QUEUES];
};

//
//
//
void
spinel_queue_pool_create(struct spinel_queue_pool *                       queue_pool,
                         VkDevice                                         d,
                         spinel_vk_context_create_info_vk_queue_t const * create_info);

//
//
//
void
spinel_queue_pool_dispose(struct spinel_queue_pool * queue_pool);

//
//
//
VkQueue
spinel_queue_pool_get_next(struct spinel_queue_pool * queue_pool);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_QUEUE_POOL_H_
