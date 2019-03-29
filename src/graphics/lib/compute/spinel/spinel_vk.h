// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPN_ONCE_SPINEL_VK
#define SPN_ONCE_SPINEL_VK

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel.h"

//
// CONTEXT CREATION: VULKAN
//

// create the vulkan context

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

#endif

//
//
//
