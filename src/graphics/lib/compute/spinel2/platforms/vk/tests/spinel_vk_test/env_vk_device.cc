// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env_vk_device.h"

#include <stdalign.h>
#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/cache.h"
#include "common/vk/debug.h"

//
// Define a platform-specific prefix
//

#ifdef __Fuchsia__
#define VK_PIPELINE_CACHE_PREFIX_STRING "/cache/."
#else
#define VK_PIPELINE_CACHE_PREFIX_STRING "."
#endif

//
//
//

using namespace spinel::vk::test;

//
//
//

env_vk_device::env_vk_device(env_vk_instance * const instance, env_spinel_vk_target * const target)
    : instance(instance), target(target)
{
  ;
}

//
//
//

void
env_vk_device::SetUp()
{
  //
  // get queue properties
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(instance->vk.pd, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[qfp_count];

  vkGetPhysicalDeviceQueueFamilyProperties(instance->vk.pd, &qfp_count, qfp);

  //
  // make sure a compute-capable queue family is at index 0
  //
  ASSERT_TRUE((qfp[0].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0);

  //
  // we only need a compute-capable queue family for this device
  //
  float const qps[] = { 1.0f };

  VkDeviceQueueCreateInfo dqcis[] = {
    {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,  // unprotected queue
      .queueFamilyIndex = 0,
      .queueCount       = 1,
      .pQueuePriorities = qps,
    },
  };

  //
  // probe Spinel device requirements for this target
  //
  struct spinel_vk_target_requirements spinel_tr = {};

  spinel_vk_target_get_requirements(target->spinel, &spinel_tr);

  //
  // extensions
  //
  char const * ext_names[spinel_tr.ext_name_count];

  //
  // features
  //
  VkPhysicalDeviceVulkan12Features pdf12 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
  };

  VkPhysicalDeviceVulkan11Features pdf11 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    .pNext = &pdf12
  };

  VkPhysicalDeviceFeatures2 pdf2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                     .pNext = &pdf11 };

  //
  // populate Spinel device requirements
  //
  spinel_tr.ext_names = ext_names;
  spinel_tr.pdf       = &pdf2.features;
  spinel_tr.pdf11     = &pdf11;
  spinel_tr.pdf12     = &pdf12;

  ASSERT_TRUE(spinel_vk_target_get_requirements(target->spinel, &spinel_tr));

  //
  // create VkDevice
  //
  VkDeviceCreateInfo const dci = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = &pdf2,
    .flags                   = 0,
    .queueCreateInfoCount    = ARRAY_LENGTH_MACRO(dqcis),
    .pQueueCreateInfos       = dqcis,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = spinel_tr.ext_name_count,
    .ppEnabledExtensionNames = spinel_tr.ext_names,
    .pEnabledFeatures        = NULL
  };

  ASSERT_TRUE(vkCreateDevice(instance->vk.pd, &dci, NULL, &vk.d) == VK_SUCCESS);

  //
  // create the pipeline cache
  //
  ASSERT_TRUE(vk_pipeline_cache_create(vk.d,  //
                                       NULL,
                                       VK_PIPELINE_CACHE_PREFIX_STRING "spinel_vk_test_cache",
                                       &vk.pc) == VK_SUCCESS);
}

//
//
//

void
env_vk_device::TearDown()
{
  ASSERT_TRUE(vk_pipeline_cache_destroy(vk.d,  //
                                        NULL,
                                        VK_PIPELINE_CACHE_PREFIX_STRING "spinel_vk_test_cache",
                                        vk.pc) == VK_SUCCESS);

  vkDestroyDevice(vk.d, NULL);
}

//
//
//
