// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdlib.h>

#include "spinel_vk_rs.h"

//
//
//

namespace spinel::vk::rs {

//
//
//
TEST(SpinelVkRs, CreateSuccess)
{
  spinel_vk_rs_instance_create_info_t const instance_create_info = {
    .is_validation = true,
    .is_debug_info = true,
  };

  VkInstance     instance;
  VkResult const result = spinel_vk_rs_instance_create(&instance_create_info, &instance);

  EXPECT_EQ(result, VK_SUCCESS);

  spinel_vk_rs_create_info_t const create_info = {
    //
    // Defaults to selecting the first device
    //
    .instance                = instance,
    .vendor_id               = 0,
    .device_id               = 0,
    .context_block_pool_size = 8 << 20,  // 8 MB
    .context_handle_count    = 8192,     // 8K handles
  };

  spinel_vk_rs_t * svr = spinel_vk_rs_create(&create_info);

  EXPECT_NE(svr, nullptr);

  spinel_vk_rs_destroy(svr);

  vkDestroyInstance(instance, NULL);
}

//
//
//
TEST(SpinelVkRs, CreateFailure)
{
  spinel_vk_rs_instance_create_info_t const instance_create_info = {
    .is_validation = true,
    .is_debug_info = true,
  };

  VkInstance     instance;
  VkResult const result = spinel_vk_rs_instance_create(&instance_create_info, &instance);

  EXPECT_EQ(result, VK_SUCCESS);

  spinel_vk_rs_create_info_t const create_info = {
    .instance                = instance,
    .vendor_id               = UINT32_MAX,  // no such vendor
    .device_id               = UINT32_MAX,  // no such device
    .context_block_pool_size = 8 << 20,     // 8 MB
    .context_handle_count    = 8192,        // 8K handles
  };

  spinel_vk_rs_t * svr = spinel_vk_rs_create(&create_info);

  EXPECT_EQ(svr, nullptr);

  vkDestroyInstance(instance, NULL);
}

//
//
//
TEST(SpinelVkRs, GetPropsIncomplete)
{
  spinel_vk_rs_instance_create_info_t const instance_create_info = {
    .is_validation = true,
    .is_debug_info = true,
  };

  VkInstance instance;
  VkResult   result = spinel_vk_rs_instance_create(&instance_create_info, &instance);

  EXPECT_EQ(result, VK_SUCCESS);

  result = spinel_vk_rs_get_physical_device_props(instance, NULL, NULL);

  EXPECT_EQ(result, VK_INCOMPLETE);

  vkDestroyInstance(instance, NULL);
}

//
//
//
TEST(SpinelVkRs, GetProps)
{
  spinel_vk_rs_instance_create_info_t const instance_create_info = {
    .is_validation = true,
    .is_debug_info = true,
  };

  VkInstance instance;
  VkResult   result = spinel_vk_rs_instance_create(&instance_create_info, &instance);

  uint32_t props_count;

  result = spinel_vk_rs_get_physical_device_props(instance, &props_count, NULL);

  EXPECT_EQ(result, VK_SUCCESS);
  EXPECT_GT(props_count, 0U);

  VkPhysicalDeviceProperties * const props = new VkPhysicalDeviceProperties[props_count];

  uint32_t props_count_new;

  result = spinel_vk_rs_get_physical_device_props(instance, &props_count_new, NULL);

  EXPECT_EQ(result, VK_SUCCESS);
  EXPECT_EQ(props_count, props_count_new);

  delete[] props;

  vkDestroyInstance(instance, NULL);
}

//
//
//

}  // namespace spinel::vk::rs

//
//
//
