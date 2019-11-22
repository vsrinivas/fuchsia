// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_SPN_VK_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_SPN_VK_TARGET_H_

//
//
//

#include <vulkan/vulkan.h>

#include "env_vk_instance.h"
#include "gtest/gtest.h"
#include "hotsort_vk.h"
#include "spinel/spinel_vk.h"

//
//
//

namespace spinel::vk::test {

//
//
//

#define spn(...) ASSERT_EQ((spn_##__VA_ARGS__), SPN_SUCCESS)

//
//
//

struct env_spn_vk_target : public ::testing::Environment
{
  env_vk_instance * const instance;

  struct spn_vk_target const *     spn;
  struct hotsort_vk_target const * hs;

  env_spn_vk_target(env_vk_instance * const instance);

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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_SPN_VK_TARGET_H_
