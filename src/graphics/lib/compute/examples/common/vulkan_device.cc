// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_device.h"

#include <stdio.h>

#include "tests/common/utils.h"
#include "tests/common/vk_surface.h"

bool
VulkanDevice::init(const VulkanDevice::Config &                 config,
                   const VulkanDevice::AppStateConfigCallback * config_callback)
{
  vk_app_state_config_t app_config = {
    .app_name            = config.app_name ? config.app_name : "VulkanDevice",
    .engine_name         = "VulkanDevice",
    .enable_validation   = config.debug,
    .enable_debug_report = config.debug,

    .require_swapchain         = config.require_swapchain,
    .disable_swapchain_present = config.disable_vsync,
  };

  if (config_callback != nullptr)
    (*config_callback)(&app_config);

  if (!vk_app_state_init(&app_state_, &app_config))
    {
      fprintf(stderr, "FAILURE\n");
      return false;
    }

  if (config.verbose)
    vk_app_state_print(&app_state_);

  vkGetDeviceQueue(app_state_.d, app_state_.qfi, 0, &graphics_queue_);
  return true;
}

VulkanDevice::~VulkanDevice()
{
  vk_app_state_destroy(&app_state_);
}
