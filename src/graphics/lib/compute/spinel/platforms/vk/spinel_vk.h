// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// CONTEXT CREATION: VULKAN
//

spn_result
spn_context_create_vk(spn_context_t                 * const context_p,
                      struct spn_device_vk          * const device_vk,
                      struct spn_target_image const * const target_image,
                      uint64_t                        const block_pool_size,
                      uint32_t                        const handle_count);

//
// RENDER EXTENSION: VULKAN BUFFER
//

struct spn_render_submit_ext_vk_buffer
{
  void                         * ext;
  spn_render_submit_ext_type_e   type; // .type = SPN_RENDER_SUBMIT_EXT_TYPE_VK_BUFFER,
  uint32_t                       pitch;
  VkDescriptorBufferInfo const * buffer;
  uint32_t                       waitSemaphoreCount;
  VkSemaphore            const * pWaitSemaphores;
  VkPipelineStageFlags   const * pWaitDstStageMask;
  uint32_t                       signalSemaphoreCount;
  VkSemaphore            const * pSignalSemaphores;
};

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//
