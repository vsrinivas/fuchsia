// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

void
vk_debug_compute_props(FILE * file, VkPhysicalDeviceProperties const * const pdp);

void
vk_debug_subgroup_props(FILE * file, VkPhysicalDeviceSubgroupProperties const * const pdsp);

VkBool32
VKAPI_PTR
vk_debug_report_cb(VkDebugReportFlagsEXT      flags,
                   VkDebugReportObjectTypeEXT objectType,
                   uint64_t                   object,
                   size_t                     location,
                   int32_t                    messageCode,
                   char const *               pLayerPrefix,
                   char const *               pMessage,
                   void       *               pUserData);

//
//
//
