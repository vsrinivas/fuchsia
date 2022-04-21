// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_SPINEL_VK_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_SPINEL_VK_TARGET_H_

//
//
//

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "env_vk_instance.h"
#include "spinel/platforms/vk/spinel_vk.h"

//
//
//

namespace spinel::vk::test {

//
//
//

#define spinel(...) ASSERT_EQ((spinel_##__VA_ARGS__), SPN_SUCCESS)

//
//
//

struct env_spinel_vk_target : public ::testing::Environment
{
  env_vk_instance * const instance;

  spinel_vk_target_t const * spinel;

  env_spinel_vk_target(env_vk_instance * const instance);

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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_SPINEL_VK_TARGET_H_
