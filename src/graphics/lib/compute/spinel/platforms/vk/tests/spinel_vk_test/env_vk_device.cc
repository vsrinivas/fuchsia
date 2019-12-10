// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env_vk_device.h"

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

env_vk_device::env_vk_device(env_vk_instance * const instance, env_spn_vk_target * const target)
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
  // probe Spinel device requirements for this target
  //
  struct spn_vk_target_requirements spn_tr = {};

  spn_vk_target_get_requirements(target->spn, &spn_tr);

  //
  // probe HotSort device requirements for this target
  //
  struct hotsort_vk_target_requirements hs_tr = {};

  hotsort_vk_target_get_requirements(target->hs, &hs_tr);

  //
  // populate accumulated device requirements
  //
  uint32_t const          ext_name_count = spn_tr.ext_name_count + hs_tr.ext_name_count;
  VkDeviceQueueCreateInfo qcis[spn_tr.qci_count];
  char const *            ext_names[ext_name_count];

  //
  // feature structures
  //
  struct
  {
    VkPhysicalDeviceHostQueryResetFeaturesEXT               a;
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR b;
    VkPhysicalDeviceScalarBlockLayoutFeaturesEXT            c;
    VkPhysicalDeviceShaderFloat16Int8FeaturesKHR            d;
    VkPhysicalDeviceSubgroupSizeControlFeaturesEXT          e;
  } features = {
    .a.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT,
    .b.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
    .c.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT,
    .d.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,
    .e.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT,
  };

  features.a.pNext = &features.b;
  features.b.pNext = &features.c;
  features.c.pNext = &features.d;
  features.d.pNext = &features.e;
  features.e.pNext = NULL;

  VkPhysicalDeviceFeatures2 pdf2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .pNext = &features.a,
  };

  //
  // populate Spinel device requirements
  //
  spn_tr.qcis      = qcis;
  spn_tr.ext_names = ext_names;
  spn_tr.pdf2      = &pdf2;

  spn(vk_target_get_requirements(target->spn, &spn_tr));

  //
  // populate HotSort device requirements
  //
  hs_tr.ext_names = ext_names + spn_tr.ext_name_count;
  hs_tr.pdf       = &pdf2.features;

  ASSERT_TRUE(hotsort_vk_target_get_requirements(target->hs, &hs_tr));

  //
  // create VkDevice
  //
  VkDeviceCreateInfo const device_info = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = pdf2.pNext,
    .flags                   = 0,
    .queueCreateInfoCount    = spn_tr.qci_count,
    .pQueueCreateInfos       = qcis,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = ext_name_count,
    .ppEnabledExtensionNames = ext_names,
    .pEnabledFeatures        = &pdf2.features
  };

  vk(CreateDevice(instance->vk.pd, &device_info, NULL, &vk.d));

  //
  // create the pipeline cache
  //
  vk_ok(vk_pipeline_cache_create(vk.d, NULL, VK_PIPELINE_CACHE_PREFIX_STRING "vk_cache", &vk.pc));
}

//
//
//

void
env_vk_device::TearDown()
{
  vk_ok(vk_pipeline_cache_destroy(vk.d, NULL, VK_PIPELINE_CACHE_PREFIX_STRING "vk_cache", vk.pc));

  vkDestroyDevice(vk.d, NULL);
}

//
//
//
