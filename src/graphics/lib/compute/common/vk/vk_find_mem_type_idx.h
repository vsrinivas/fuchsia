// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_FIND_MEM_TYPE_IDX_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_FIND_MEM_TYPE_IDX_H_

//
//
//

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

uint32_t
vk_find_mem_type_idx(VkPhysicalDeviceMemoryProperties const * pdmp,
                     uint32_t                                 memoryTypeBits,
                     VkMemoryPropertyFlags const              mpf);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_FIND_MEM_TYPE_IDX_H_
