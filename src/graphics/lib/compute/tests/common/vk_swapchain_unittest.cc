// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/vk_swapchain.h"

#include <gtest/gtest.h>

#include "tests/common/vk_app_state.h"
#include "tests/common/vk_device_surface_info.h"
#include "tests/common/vk_surface.h"

// Helper function to create a vk_surface_t from an vk_app_state_t
// This should only be used to verify that surface creation works
// when the configuration included "require_swapchain".
static vk_surface_t *
CreateSurface(vk_app_state_t * app_state)
{
  vk_surface_config_t config = {
    .instance           = app_state->instance,
    .physical_device    = app_state->pd,
    .queue_family_index = app_state->qfi,
    .allocator          = app_state->ac,
  };
  return vk_surface_create(&config);
}

TEST(vkSwapchainTest, CreateSwapchainWithDefaultConfig)
{
  vk_app_state_t app = {};

  const vk_app_state_config_t config = {
    .require_swapchain = true,
  };

  ASSERT_TRUE(vk_app_state_init(&app, &config)) << "Could not initialize vk_app_state_t instance";

  vk_surface_t * surface = CreateSurface(&app);
  ASSERT_TRUE(surface);

  vk_swapchain_config_t swapchain_config = {
    .instance        = app.instance,
    .device          = app.d,
    .physical_device = app.pd,
    .allocator       = app.ac,

    .present_queue_family = app.qfi,
    .present_queue_index  = 0u,

    .surface_khr = vk_surface_get_surface_khr(surface),
  };
  vk_swapchain_t * swapchain = vk_swapchain_create(&swapchain_config);
  ASSERT_TRUE(swapchain);

  vk_swapchain_destroy(swapchain);

  vk_surface_destroy(surface);
  vk_app_state_destroy(&app);
}

TEST(vkSwapchainTest, CreateSwapchainWithSwapchainStaging)
{
  vk_app_state_t app = {};

  const vk_app_state_config_t config = {
    .enable_validation = true,
    .require_swapchain = true,
  };

  ASSERT_TRUE(vk_app_state_init(&app, &config)) << "Could not initialize vk_app_state_t instance";

  vk_surface_t * surface = CreateSurface(&app);
  ASSERT_TRUE(surface);

  VkSurfaceKHR             surface_khr = vk_surface_get_surface_khr(surface);
  vk_device_surface_info_t surface_info;
  vk_device_surface_info_init(&surface_info, app.pd, surface_khr, app.instance);

  ASSERT_NE(surface_info.formats_count, 0u) << "At least one presentable surface format required!";

  vk_swapchain_config_t swapchain_config = {
    .instance        = app.instance,
    .device          = app.d,
    .physical_device = app.pd,
    .allocator       = app.ac,

    .present_queue_family = app.qfi,
    .present_queue_index  = 0u,

    .surface_khr  = surface_khr,
    .pixel_format = surface_info.formats[0].format,
    .staging_mode = VK_SWAPCHAIN_STAGING_MODE_FORCED,
  };
  vk_swapchain_t * swapchain = vk_swapchain_create(&swapchain_config);
  ASSERT_TRUE(swapchain);

  vk_swapchain_destroy(swapchain);

  vk_device_surface_info_destroy(&surface_info);
  vk_surface_destroy(surface);
  vk_app_state_destroy(&app);
}
