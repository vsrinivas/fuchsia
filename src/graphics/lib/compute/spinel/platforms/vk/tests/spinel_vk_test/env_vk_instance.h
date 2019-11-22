// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_VK_INSTANCE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_VK_INSTANCE_H_

//
//
//

#include <vulkan/vulkan.h>

#include "gtest/gtest.h"

//
//
//

namespace spinel::vk::test {

//
//
//

#define vk(...) ASSERT_EQ((vk##__VA_ARGS__), VK_SUCCESS)
#define vk_ok(err) ASSERT_EQ(err, VK_SUCCESS)

//
//
//

struct env_vk_instance : public ::testing::Environment
{
  uint32_t vendorID;
  uint32_t deviceID;

  struct
  {
    VkInstance                       i;
    VkPhysicalDevice                 pd;
    VkPhysicalDeviceProperties       pdp;
    VkPhysicalDeviceMemoryProperties pdmp;
    VkDebugReportCallbackEXT         drc;
  } vk;

  env_vk_instance(uint32_t vendorID, uint32_t deviceID);

  void
  SetUp() override;

  void
  TearDown() override;
};

//
//
//

}  // namespace spinel::vk::test

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_VK_INSTANCE_H_
