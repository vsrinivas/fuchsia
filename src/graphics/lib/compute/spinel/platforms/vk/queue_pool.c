// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "queue_pool.h"

#include "common/macros.h"
#include "common/vk/assert.h"

//
//
//
void
spinel_queue_pool_create(struct spinel_queue_pool *                       queue_pool,
                         VkDevice                                         d,
                         spinel_vk_context_create_info_vk_queue_t const * create_info)
{
  uint32_t const qc_clamp = MIN_MACRO(uint32_t, SPN_QUEUE_POOL_MAX_QUEUES, create_info->count);

  *queue_pool = (struct spinel_queue_pool){
    .create_info = *create_info,
    .queue_next  = 0,
  };

  VkDeviceQueueInfo2 dqi2 = {
    .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
    .pNext            = NULL,
    .flags            = create_info->flags,
    .queueFamilyIndex = create_info->family_index,
    /* .queueIndex    = [0, qc_clamp) */
  };

  for (uint32_t ii = 0; ii < qc_clamp; ii++)
    {
      dqi2.queueIndex = ii;

      vkGetDeviceQueue2(d, &dqi2, queue_pool->queues + ii);
    }
}

//
//
//
void
spinel_queue_pool_dispose(struct spinel_queue_pool * queue_pool)
{
  ;  // Nothing to do
}

//
//
//
VkQueue
spinel_queue_pool_get_next(struct spinel_queue_pool * queue_pool)
{
  assert(queue_pool->create_info.count > 0);

  return queue_pool->queues[queue_pool->queue_next++ % queue_pool->create_info.count];
}

//
//
//
