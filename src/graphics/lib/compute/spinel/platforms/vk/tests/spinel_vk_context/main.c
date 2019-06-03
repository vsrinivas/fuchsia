// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/vk_assert.h"
#include "common/vk/vk_cache.h"
#include "common/vk/vk_debug.h"
#include "spinel_assert.h"
#include "spinel_vk.h"

//
// Spinel targets
//

#include "targets/vendors/amd/gcn3/spn_target.h"
#include "targets/vendors/intel/gen8/spn_target.h"
#include "targets/vendors/nvidia/sm50/spn_target.h"

//
//
//

static struct spn_vk_target const *
find_target(uint32_t const vendor_id, uint32_t const device_id)
{
  switch (vendor_id)
    {
        case 0x10DE:  // NVIDIA
        {
          //
          // FIXME -- for now, the kernels in this app are targeting
          // sm_35+ devices.  You could add some rigorous rejection by
          // device id here...
          //
          return spn_nvidia_sm50;
        }
        case 0x8086:  // INTEL
        {
          //
          // FIXME -- for now, the kernels in this app are targeting GEN8+
          // devices -- this does *not* include variants of GEN9LP+
          // "Apollo Lake" because that device has a different
          // architectural "shape" than GEN8 GTx.  You could add some
          // rigorous rejection by device id here...
          //
          return spn_intel_gen8;
        }
        case 0x1002:  // AMD
        {
          //
          // AMD GCN
          //
          return spn_amd_gcn3;
        }
        case 0x13B5: {
          //
          // ARM BIFROST
          //
          if (device_id == 0x1234)
            {
              //
              // BIFROST GEN1 - subgroupSize = 4
              //
              fprintf(stdout, "Detected Bifrost4...\n");
              return NULL;  // spn_arm_bifrost4;
            }
          else if (device_id == 0x5678)
            {
              //
              // BIFROST GEN2 - subgroupSize = 8
              //
              fprintf(stdout, "Detected Bifrost8...\n");
              return NULL;  // spn_arm_bifrost8;
            }
          else
            {
              return NULL;
            }
        }
      default:
        return NULL;
    }
}

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
                                       .pApplicationName   = "Fuchsia Spinel/VK Test",
                                       .applicationVersion = 0,
                                       .pEngineName        = "Fuchsia Spinel/VK",
                                       .engineVersion      = 0,
                                       .apiVersion         = VK_API_VERSION_1_1 };

  char const * const instance_enabled_layers[] = { "VK_LAYER_LUNARG_standard_validation", NULL };

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
  //
  //
#ifndef NDEBUG
  PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
    (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance,
                                                              "vkCreateDebugReportCallbackEXT");

  PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
    (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance,
                                                               "vkDestroyDebugReportCallbackEXT");

  struct VkDebugReportCallbackCreateInfoEXT const drcci = {
    .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
    .pNext       = NULL,
    .flags       = (VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
              VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT |
              VK_DEBUG_REPORT_DEBUG_BIT_EXT),
    .pfnCallback = vk_debug_report_cb,
    .pUserData   = NULL
  };

  VkDebugReportCallbackEXT drc;

  vk(CreateDebugReportCallbackEXT(instance, &drcci, NULL, &drc));
#endif

  //
  // Prepare Vulkan environment for Spinel
  //
  struct spn_vk_environment environment = { .d   = VK_NULL_HANDLE,
                                            .ac  = NULL,
                                            .pc  = VK_NULL_HANDLE,
                                            .pd  = VK_NULL_HANDLE,
                                            .qfi = 0 };

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
  VkPhysicalDeviceProperties pdp;

  vkGetPhysicalDeviceProperties(pds[0], &pdp);

  uint32_t const vendor_id = (argc <= 2) ? pdp.vendorID : strtoul(argv[1], NULL, 16);
  uint32_t const device_id = (argc <= 2) ? pdp.deviceID : strtoul(argv[2], NULL, 16);

  //
  // list all devices
  //
  environment.pd = VK_NULL_HANDLE;

  for (uint32_t ii = 0; ii < pd_count; ii++)
    {
      vkGetPhysicalDeviceProperties(pds[ii], &pdp);

      bool const is_match = (pdp.vendorID == vendor_id) && (pdp.deviceID == device_id);

      if (is_match)
        {
          environment.pd = pds[ii];
        }

      fprintf(stdout,
              "%c %X : %X : %s\n",
              is_match ? '*' : ' ',
              pdp.vendorID,
              pdp.deviceID,
              pdp.deviceName);
    }

  if (environment.pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Device %X : %X not found.\n", vendor_id, device_id);

      return EXIT_FAILURE;
    }

  free(pds);

  //
  // get the physical device's memory props
  //
  vkGetPhysicalDeviceMemoryProperties(environment.pd, &environment.pdmp);

  //
  // get queue properties
  //
  VkQueueFamilyProperties qfp[2];
  uint32_t                qfc = ARRAY_LENGTH_MACRO(qfp);

  vkGetPhysicalDeviceQueueFamilyProperties(environment.pd, &qfc, qfp);

  //
  // get image properties
  //
  // vkGetPhysicalDeviceImageFormatProperties()
  //
  // vk(GetPhysicalDeviceImageFormatProperties(phy_device,
  //

  //
  // create device
  //
  float const qp[] = { 1.0f };

  VkDeviceQueueCreateInfo const qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                       .pNext = NULL,
                                       .flags = 0,
                                       .queueFamilyIndex = environment.qfi,
                                       .queueCount       = 1,
                                       .pQueuePriorities = qp };

  //
  // enable AMD shader info extension?
  //
#if defined(SPN_VK_SHADER_INFO_AMD_STATISTICS) || defined(SPN_VK_SHADER_INFO_AMD_DISASSEMBLY)
  char const * const device_enabled_extensions[] = {
    // VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
    VK_AMD_SHADER_INFO_EXTENSION_NAME,
  };
  uint32_t const device_enabled_extension_count =
    ARRAY_LENGTH_MACRO(device_enabled_extensions) - ((pdp.vendor_id != 0x1002) ? 1 : 0);
#else
  char const * const device_enabled_extensions[]    = {};
  uint32_t const     device_enabled_extension_count = 0;
#endif

  //
  //
  //
  VkPhysicalDeviceFeatures device_features = { false };

  //
  // FIXME -- for now, HotSort requires 'shaderInt64'
  //
  if (true /*key_val_words == 2*/)
    {
      //
      // FIXME
      //
      // SEGMENT_TTCK and SEGMENT_TTRK shaders benefit from
      // shaderInt64 but shaderFloat64 shouldn't be required.
      //
      device_features.shaderInt64   = true;
      device_features.shaderFloat64 = true;
    }

  VkDeviceCreateInfo const device_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                           .pNext = NULL,
                                           .flags = 0,
                                           .queueCreateInfoCount  = 1,
                                           .pQueueCreateInfos     = &qi,
                                           .enabledLayerCount     = 0,
                                           .ppEnabledLayerNames   = NULL,
                                           .enabledExtensionCount = device_enabled_extension_count,
                                           .ppEnabledExtensionNames = device_enabled_extensions,
                                           .pEnabledFeatures        = &device_features };

  vk(CreateDevice(environment.pd, &device_info, NULL, &environment.d));

  //
  // create the pipeline cache
  //
  vk(_pipeline_cache_create(environment.d, NULL, ".vk_cache", &environment.pc));

  //
  // find spinel target
  //
  struct spn_vk_context_create_info const create_info = {
    .target          = find_target(vendor_id, device_id),
    .block_pool_size = 128 << 20,  // 128 MB block pool
    .handle_count    = 1 << 17     // 128K handles
  };

  if (create_info.target == NULL)
    {
      fprintf(stderr, "Device %X : %X has no target.\n", vendor_id, device_id);

      return EXIT_FAILURE;
    }

  //
  // create a Spinel context
  //
  spn_context_t context;

  spn(vk_context_create(&environment, &create_info, &context));

  //
  // release the context
  //
  spn(context_release(context));

  //
  // dispose of Vulkan resources
  //
  vk(_pipeline_cache_destroy(environment.d, NULL, ".vk_cache", environment.pc));

  vkDestroyDevice(environment.d, NULL);

#ifndef NDEBUG
  vkDestroyDebugReportCallbackEXT(instance, drc, NULL);
#endif

  vkDestroyInstance(instance, NULL);

  return EXIT_SUCCESS;
}

//
//
//
