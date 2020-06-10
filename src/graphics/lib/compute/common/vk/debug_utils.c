// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "debug_utils.h"

//
//
//

#define DU_PFN_NAME(name_) pfn_##name_

#define DU_PFN_TYPE(name_) PFN_##name_

#define DU_PFN_INIT(instance_, name_)                                                              \
  DU_PFN_NAME(name_) = (DU_PFN_TYPE(name_))vkGetInstanceProcAddr(instance_, #name_)

//
// The pfns default to NULL
//

// clang-format off
DU_PFN_TYPE(vkSetDebugUtilsObjectNameEXT)    DU_PFN_NAME(vkSetDebugUtilsObjectNameEXT)    = NULL;
DU_PFN_TYPE(vkSetDebugUtilsObjectTagEXT)     DU_PFN_NAME(vkSetDebugUtilsObjectTagEXT)     = NULL;
DU_PFN_TYPE(vkQueueBeginDebugUtilsLabelEXT)  DU_PFN_NAME(vkQueueBeginDebugUtilsLabelEXT)  = NULL;
DU_PFN_TYPE(vkQueueEndDebugUtilsLabelEXT)    DU_PFN_NAME(vkQueueEndDebugUtilsLabelEXT)    = NULL;
DU_PFN_TYPE(vkQueueInsertDebugUtilsLabelEXT) DU_PFN_NAME(vkQueueInsertDebugUtilsLabelEXT) = NULL;
DU_PFN_TYPE(vkCmdBeginDebugUtilsLabelEXT)    DU_PFN_NAME(vkCmdBeginDebugUtilsLabelEXT)    = NULL;
DU_PFN_TYPE(vkCmdEndDebugUtilsLabelEXT)      DU_PFN_NAME(vkCmdEndDebugUtilsLabelEXT)      = NULL;
DU_PFN_TYPE(vkCmdInsertDebugUtilsLabelEXT)   DU_PFN_NAME(vkCmdInsertDebugUtilsLabelEXT)   = NULL;
DU_PFN_TYPE(vkCreateDebugUtilsMessengerEXT)  DU_PFN_NAME(vkCreateDebugUtilsMessengerEXT)  = NULL;
DU_PFN_TYPE(vkSubmitDebugUtilsMessageEXT)    DU_PFN_NAME(vkSubmitDebugUtilsMessageEXT)    = NULL;
// clang-format on

//
// Initialize debug util instance extension pfns.
//

void
vk_debug_utils_init(VkInstance instance)
{
  DU_PFN_INIT(instance, vkSetDebugUtilsObjectNameEXT);
  DU_PFN_INIT(instance, vkSetDebugUtilsObjectTagEXT);
  DU_PFN_INIT(instance, vkQueueBeginDebugUtilsLabelEXT);
  DU_PFN_INIT(instance, vkQueueEndDebugUtilsLabelEXT);
  DU_PFN_INIT(instance, vkQueueInsertDebugUtilsLabelEXT);
  DU_PFN_INIT(instance, vkCmdBeginDebugUtilsLabelEXT);
  DU_PFN_INIT(instance, vkCmdEndDebugUtilsLabelEXT);
  DU_PFN_INIT(instance, vkCmdInsertDebugUtilsLabelEXT);
  DU_PFN_INIT(instance, vkCreateDebugUtilsMessengerEXT);
  DU_PFN_INIT(instance, vkSubmitDebugUtilsMessageEXT);
}

//
//
//
