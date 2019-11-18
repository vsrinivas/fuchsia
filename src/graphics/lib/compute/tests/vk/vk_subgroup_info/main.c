// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Simple Vulkan example that prints out compute and subgroup
// properties that aren't reported by vulkaninfo.
//

#include <stdio.h>
#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/find_validation_layer.h"
#include "common/vk/assert.h"
#include "common/vk/debug.h"

//
//
//

int
main(int argc, char const * argv[])
{
  //
  // create a Vulkan instances
  //
  VkApplicationInfo const app_info = { .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                       .pNext              = NULL,
                                       .pApplicationName   = "Fuchsia Vulkan Subgroup Info",
                                       .applicationVersion = 0,
                                       .pEngineName        = "Fuchsia Vulkan",
                                       .engineVersion      = 0,
                                       .apiVersion         = VK_API_VERSION_1_1 };

  char const * const instance_enabled_layers[] = { vk_find_validation_layer(), NULL };

  char const * const instance_enabled_extensions[] = { VK_EXT_DEBUG_REPORT_EXTENSION_NAME, NULL };

  VkInstanceCreateInfo const instance_info = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = ARRAY_LENGTH_MACRO(instance_enabled_layers) - 1,
    .ppEnabledLayerNames     = instance_enabled_layers,
    .enabledExtensionCount   = ARRAY_LENGTH_MACRO(instance_enabled_extensions) - 1,
    .ppEnabledExtensionNames = instance_enabled_extensions
  };

  VkInstance instance;

  vk(CreateInstance(&instance_info, NULL, &instance));

  //
  // various debug reports
  //
#ifndef NDEBUG
  PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
    (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance,
                                                              "vkCreateDebugReportCallbackEXT");

  PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
    (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance,
                                                               "vkDestroyDebugReportCallbackEXT");

  struct VkDebugReportCallbackCreateInfoEXT const drcci = {
    .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
    .pNext = NULL,
    .flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
             VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT |
             VK_DEBUG_REPORT_DEBUG_BIT_EXT,
    .pfnCallback = vk_debug_report_cb,
    .pUserData   = NULL
  };

  VkDebugReportCallbackEXT drc;

  vk(CreateDebugReportCallbackEXT(instance, &drcci, NULL, &drc));
#endif

  //
  // acquire all physical devices
  //
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(instance, &pd_count, NULL));

  if (pd_count == 0)
    {
      fprintf(stderr, "No device found\n");

      return EXIT_FAILURE;
    }

  VkPhysicalDevice * pds = malloc(pd_count * sizeof(*pds));

  vk(EnumeratePhysicalDevices(instance, &pd_count, pds));

  //
  // select the first device if *both* ids aren't provided
  //
  // acquire these properties for each device
  //
  VkPhysicalDeviceSubgroupProperties pdsp = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    .pNext = NULL
  };

  VkPhysicalDeviceProperties2 pdp2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                       .pNext = &pdsp };

  vkGetPhysicalDeviceProperties2(pds[0], &pdp2);

  uint32_t const vendor_id = (argc <= 2) ? pdp2.properties.vendorID : strtoul(argv[1], NULL, 16);
  uint32_t const device_id = (argc <= 2) ? pdp2.properties.deviceID : strtoul(argv[2], NULL, 16);

  //
  // list all devices
  //
  VkPhysicalDevice pd = VK_NULL_HANDLE;

  for (uint32_t ii = 0; ii < pd_count; ii++)
    {
      vkGetPhysicalDeviceProperties2(pds[ii], &pdp2);

      bool const is_match =
        (pdp2.properties.vendorID == vendor_id) && (pdp2.properties.deviceID == device_id);

      if (is_match)
        {
          pd = pds[ii];
        }

      fprintf(stdout,
              "%c %X : %X : %s\n",
              is_match ? '*' : ' ',
              pdp2.properties.vendorID,
              pdp2.properties.deviceID,
              pdp2.properties.deviceName);
    }

  if (pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Device %X : %X not found.\n", vendor_id, device_id);

      return EXIT_FAILURE;
    }

  free(pds);

  //
  // report the match
  //
  vkGetPhysicalDeviceProperties2(pd, &pdp2);

  vk_debug_compute_props(stdout, &pdp2.properties);
  vk_debug_subgroup_props(stdout, &pdsp);

  //
  // cleanup
  //
#ifndef NDEBUG
  vkDestroyDebugReportCallbackEXT(instance, drc, NULL);
#endif

  vkDestroyInstance(instance, NULL);

  return EXIT_SUCCESS;
}

//
//
//
