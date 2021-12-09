// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_VK_DEVICE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_VK_DEVICE_H_

//
//
//

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "env_spinel_vk_target.h"

//
//
//

namespace spinel::vk::test {

//
//
//

struct env_vk_device : public ::testing::Environment
{
  env_vk_instance * const      instance;
  env_spinel_vk_target * const target;

  struct
  {
    VkDevice        d;
    VkPipelineCache pc;
  } vk;

  env_vk_device(env_vk_instance * const instance, env_spinel_vk_target * const target);

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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_VK_DEVICE_H_
