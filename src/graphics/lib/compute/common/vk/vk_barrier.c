// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "vk_barrier.h"

//
//
//

void
vk_barrier_compute_w_to_compute_r(VkCommandBuffer cb)
{
  static VkMemoryBarrier const w_to_r = {.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                         .pNext         = NULL,
                                         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};

  vkCmdPipelineBarrier(cb,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       0,
                       1,
                       &w_to_r,
                       0,
                       NULL,
                       0,
                       NULL);
}

//
//
//

void
vk_barrier_compute_w_to_transfer_r(VkCommandBuffer cb)
{
  static VkMemoryBarrier const compute_w_to_transfer_r = {
    .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext         = NULL,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};

  vkCmdPipelineBarrier(cb,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,
                       1,
                       &compute_w_to_transfer_r,
                       0,
                       NULL,
                       0,
                       NULL);
}

//
//
//
