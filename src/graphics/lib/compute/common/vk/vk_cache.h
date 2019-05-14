// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_CACHE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_CACHE_H_

//
//
//

#include <vulkan/vulkan.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

VkResult
vk_pipeline_cache_create(VkDevice                      device,
                         VkAllocationCallbacks const * allocator,
                         char const * const            name,
                         VkPipelineCache *             pipeline_cache);

VkResult
vk_pipeline_cache_destroy(VkDevice                      device,
                          VkAllocationCallbacks const * allocator,
                          char const * const            name,
                          VkPipelineCache               pipeline_cache);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_CACHE_H_
