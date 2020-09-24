// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_surface.h"

#include <gtest/gtest.h>

#include "vk_app_state.h"

TEST(VkSurface, Creation)
{
  vk_app_state_t app = {};

  const vk_app_state_config_t app_config = {
    .require_swapchain = true,
  };

  ASSERT_TRUE(vk_app_state_init(&app, &app_config))
    << "Could not initialize vk_app_state_t instance";

  vk_surface_config_t config = {
    .instance           = app.instance,
    .physical_device    = app.pd,
    .queue_family_index = app.qfi,
    .allocator          = app.ac,
  };
  vk_surface * surface = vk_surface_create(&config);
  ASSERT_TRUE(surface) << "Could not create surface!";

  vk_surface_destroy(surface);

  vk_app_state_destroy(&app);
}

TEST(VkSurface, CreationWithDisableSwapchainPresent)
{
  vk_app_state_t app = {};

  const vk_app_state_config_t app_config = {
    .require_swapchain         = true,
    .disable_swapchain_present = true,
  };

  ASSERT_TRUE(vk_app_state_init(&app, &app_config))
    << "Could not initialize vk_app_state_t instance";

  vk_surface_config_t config = {
    .instance           = app.instance,
    .physical_device    = app.pd,
    .queue_family_index = app.qfi,
    .allocator          = app.ac,
  };
  vk_surface * surface = vk_surface_create(&config);
  ASSERT_TRUE(surface) << "Could not create surface!";

  vk_surface_destroy(surface);

  vk_app_state_destroy(&app);
}
