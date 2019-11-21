// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "env_vk_device.h"

#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/cache.h"
#include "common/vk/debug.h"
#include "spinel/spinel_assert.h"

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

  spn(vk_target_get_requirements(target->spn, &spn_tr));

  //
  // probe HotSort device requirements for this target
  //
  struct hotsort_vk_target_requirements hs_tr = {};

  ASSERT_TRUE(hotsort_vk_target_get_requirements(target->hs, &hs_tr));

  //
  // populate accumulated device requirements
  //
  uint32_t const           ext_name_count = spn_tr.ext_name_count + hs_tr.ext_name_count;
  VkDeviceQueueCreateInfo  qcis[spn_tr.qci_count];
  char const *             ext_names[ext_name_count];
  VkPhysicalDeviceFeatures pdf = {};

  //
  // populate Spinel device requirements
  //
  spn_tr.qcis      = qcis;
  spn_tr.ext_names = ext_names;
  spn_tr.pdf       = &pdf;

  spn(vk_target_get_requirements(target->spn, &spn_tr));

  //
  // populate HotSort device requirements
  //
  hs_tr.ext_names = ext_names + spn_tr.ext_name_count;
  hs_tr.pdf       = &pdf;

  ASSERT_TRUE(hotsort_vk_target_get_requirements(target->hs, &hs_tr));

  //
  // create VkDevice
  //
  VkDeviceCreateInfo const device_info = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = NULL,  // &shaderFloat16Int8,
    .flags                   = 0,
    .queueCreateInfoCount    = spn_tr.qci_count,
    .pQueueCreateInfos       = qcis,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = ext_name_count,
    .ppEnabledExtensionNames = ext_names,
    .pEnabledFeatures        = &pdf
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
