// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_DEBUG_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_DEBUG_UTILS_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Initialize debug util instance extension pfns.
//
// If the "VK_EXT_debug_utils" layer was not enabled then the pfns will
// remain NULL.
//

void
vk_debug_utils_init(VkInstance instance);

//
// The pfns are always NULL unless the instance has been created with
// the "VK_EXT_debug_utils" extension enabled.
//

extern PFN_vkSetDebugUtilsObjectNameEXT    pfn_vkSetDebugUtilsObjectNameEXT;
extern PFN_vkSetDebugUtilsObjectTagEXT     pfn_vkSetDebugUtilsObjectTagEXT;
extern PFN_vkQueueBeginDebugUtilsLabelEXT  pfn_vkQueueBeginDebugUtilsLabelEXT;
extern PFN_vkQueueEndDebugUtilsLabelEXT    pfn_vkQueueEndDebugUtilsLabelEXT;
extern PFN_vkQueueInsertDebugUtilsLabelEXT pfn_vkQueueInsertDebugUtilsLabelEXT;
extern PFN_vkCmdBeginDebugUtilsLabelEXT    pfn_vkCmdBeginDebugUtilsLabelEXT;
extern PFN_vkCmdEndDebugUtilsLabelEXT      pfn_vkCmdEndDebugUtilsLabelEXT;
extern PFN_vkCmdInsertDebugUtilsLabelEXT   pfn_vkCmdInsertDebugUtilsLabelEXT;
extern PFN_vkCreateDebugUtilsMessengerEXT  pfn_vkCreateDebugUtilsMessengerEXT;
extern PFN_vkSubmitDebugUtilsMessageEXT    pfn_vkSubmitDebugUtilsMessageEXT;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_DEBUG_UTILS_H_
