// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env_vk_instance.h"

#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/debug.h"

//
//
//

#define VK_GET_INSTANCE_PROC_ADDR(inst_, name_)                                                    \
  PFN_vk##name_ vk##name_ = (PFN_vk##name_)vkGetInstanceProcAddr(inst_, "vk" #name_)

//
//
//

using namespace spinel::vk::test;

//
//
//

env_vk_instance::env_vk_instance(uint32_t vendorID, uint32_t deviceID)
    : vendorID(vendorID), deviceID(deviceID)
{
  ;
}

//
//
//

void
env_vk_instance::SetUp()
{
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = "Fuchsia Spinel/VK Test",
    .applicationVersion = 0,
    .pEngineName        = "Fuchsia Spinel/VK",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_1
  };

  //
  // programmatically enable tracing
  //
  char const * const instance_enabled_layers[] = {
    "VK_LAYER_KHRONOS_validation",
  };

  char const * const instance_enabled_extensions[] = {
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
  };

  uint32_t const instance_enabled_layer_count     = ARRAY_LENGTH_MACRO(instance_enabled_layers);
  uint32_t const instance_enabled_extension_count = ARRAY_LENGTH_MACRO(instance_enabled_extensions);

  VkInstanceCreateInfo const instance_info = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = instance_enabled_layer_count,
    .ppEnabledLayerNames     = instance_enabled_layers,
    .enabledExtensionCount   = instance_enabled_extension_count,
    .ppEnabledExtensionNames = instance_enabled_extensions
  };

  vk(CreateInstance(&instance_info, NULL, &vk.i));

  //
  // instance pfn's
  //
  VK_GET_INSTANCE_PROC_ADDR(vk.i, CreateDebugReportCallbackEXT);

  struct VkDebugReportCallbackCreateInfoEXT const drcci = {

    .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
    .pNext = NULL,
    .flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT |          //
             VK_DEBUG_REPORT_WARNING_BIT_EXT |              //
             VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |  //
             VK_DEBUG_REPORT_ERROR_BIT_EXT,                 /*| VK_DEBUG_REPORT_DEBUG_BIT_EXT,*/
    .pfnCallback = vk_debug_report_cb,
    .pUserData   = NULL
  };

  vk(CreateDebugReportCallbackEXT(vk.i, &drcci, NULL, &vk.drc));

  //
  // acquire all physical devices
  //
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(vk.i, &pd_count, NULL));

  ASSERT_TRUE(pd_count > 0) << "No device found\n";

  VkPhysicalDevice pds[pd_count];

  vk(EnumeratePhysicalDevices(vk.i, &pd_count, pds));

  // if 0's then select first
  if ((vendorID == 0) && (deviceID == 0))
    {
      vk.pd = pds[0];

      // get physical device props
      vkGetPhysicalDeviceProperties(vk.pd, &vk.pdp);
    }
  else
    {
      uint32_t ii = 0;

      for (; ii < pd_count; ii++)
        {
          vk.pd = pds[ii];

          // get physical device props
          vkGetPhysicalDeviceProperties(vk.pd, &vk.pdp);

          if ((vk.pdp.vendorID == vendorID) && (vk.pdp.deviceID == deviceID))
            {
              break;
            }
        }

      ASSERT_FALSE(ii == pd_count) << "No device matching: "  //
                                   << std::hex                //
                                   << vendorID << " : " << deviceID;
    }

  // get physical device memory props
  vkGetPhysicalDeviceMemoryProperties(vk.pd, &vk.pdmp);

  //
  // get image properties
  //
  // NOTE(allanmac): we may care about this in the future but not now
  //
  // vkGetPhysicalDeviceImageFormatProperties(...)
  //
}

//
//
//

void
env_vk_instance::TearDown()
{
  VK_GET_INSTANCE_PROC_ADDR(vk.i, DestroyDebugReportCallbackEXT);

  vkDestroyDebugReportCallbackEXT(vk.i, vk.drc, NULL);
  vkDestroyInstance(vk.i, NULL);
}

//
//
//
