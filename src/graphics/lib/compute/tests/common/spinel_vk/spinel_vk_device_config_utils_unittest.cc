// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_vk_device_config_utils.h"

#include <gtest/gtest.h>

TEST(SpinelVkUtils, spinel_vk_device_config_callback)
{
  // Initialize Vulkan device with all features required by Spinel + Hotsort.
  spinel_vk_device_configuration_t spinel_config = {};

  vk_app_state_config_t app_config = {
    .device_config_callback = spinel_vk_device_config_callback,
    .device_config_opaque   = &spinel_config,
  };

  vk_app_state_t app;
  ASSERT_TRUE(vk_app_state_init(&app, &app_config))
    << "Vulkan device creation failed unexpectedly!";

  EXPECT_TRUE(spinel_config.spinel_target);
  EXPECT_TRUE(spinel_config.hotsort_target);

  vk_app_state_destroy(&app);
}
