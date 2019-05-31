// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPINEL_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPINEL_VK_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel.h"
#include "spinel_vk_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// CONTEXT CREATION: VULKAN
//

struct spn_vk_environment
{
  VkDevice                         d;
  VkAllocationCallbacks const *    ac;
  VkPipelineCache                  pc;
  VkPhysicalDevice                 pd;
  VkPhysicalDeviceMemoryProperties pdmp;
  uint32_t                         qfi;  // queue family index
};

struct spn_vk_context_create_info
{
  struct spn_vk_target const * target;
  uint64_t                     block_pool_size;
  uint32_t                     handle_count;
};

spn_result
spn_vk_context_create(struct spn_vk_environment * const               environment,
                      struct spn_vk_context_create_info const * const create_info,
                      spn_context_t * const                           context);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPINEL_VK_H_
