// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_H_

//
//
//

#include "env_spn_vk_target.h"
#include "env_vk_device.h"
#include "env_vk_instance.h"

//
//
//

namespace spinel::vk::test {

//
// NOTE(allanmac): These environments were intended to be registered in
// a custom main() but the Fuchsia-integrated gtest main() is currently
// highly customized.
//
// // register with gtest
// ::testing::AddGlobalTestEnvironment(env::instance);
// ::testing::AddGlobalTestEnvironment(env::target);
// ::testing::AddGlobalTestEnvironment(env::device);
//

struct env
{
  std::unique_ptr<env_vk_instance>   instance;
  std::unique_ptr<env_spn_vk_target> target;
  std::unique_ptr<env_vk_device>     device;

  env();

  void
  GlobalSetUp();

  void
  GlobalTearDown();
};

//
//
//

}  // namespace spinel::vk::test

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TESTS_SPINEL_VK_TEST_ENV_H_
