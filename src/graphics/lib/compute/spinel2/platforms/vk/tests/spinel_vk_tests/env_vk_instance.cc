// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env_vk_instance.h"

#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/debug_utils.h"

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
    .apiVersion         = VK_API_VERSION_1_2
  };

  //
  // programmatically enable tracing
  //
  char const * const instance_layers[] = {
    //
    // additional layers here...
    //
    "VK_LAYER_KHRONOS_validation"  // keep this name last
  };

  char const * const instance_extensions[] = {
    //
    // additional extensions here...
    //
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
  };

  uint32_t const instance_layer_count     = ARRAY_LENGTH_MACRO(instance_layers);
  uint32_t const instance_extension_count = ARRAY_LENGTH_MACRO(instance_extensions);

  VkInstanceCreateInfo const instance_info = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = instance_layer_count,
    .ppEnabledLayerNames     = instance_layers,
    .enabledExtensionCount   = instance_extension_count,
    .ppEnabledExtensionNames = instance_extensions
  };

  vk(CreateInstance(&instance_info, NULL, &vk.i));

  //
  // Initialize debug util pfns
  //
  vk_debug_utils_init(vk.i);

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

  //
  // get physical device memory props
  //
  vkGetPhysicalDeviceMemoryProperties(vk.pd, &vk.pdmp);
}

//
//
//

void
env_vk_instance::TearDown()
{
  vkDestroyInstance(vk.i, NULL);
}

//
//
//
