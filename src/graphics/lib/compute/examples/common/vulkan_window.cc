// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_window.h"

#include <stdio.h>

#include "tests/common/utils.h"
#include "tests/common/vk_app_state.h"
#include "tests/common/vk_device_surface_info.h"
#include "tests/common/vk_surface.h"
#include "tests/common/vk_swapchain.h"
#include "tests/common/vk_swapchain_queue.h"
#include "vulkan_device.h"

bool
VulkanWindow::init(VulkanDevice * device, const VulkanWindow::Config & config)
{
  device_ = device;

  // Allocate display surface, and determine whether it's possible to directly
  // render to the swapchain with it.
  const vk_surface_config_t surface_config = {
    .instance           = device->vk_instance(),
    .physical_device    = device->vk_physical_device(),
    .queue_family_index = device->graphics_queue_family(),
    .allocator          = device->vk_allocator(),
    .window_width       = config.window_width,
    .window_height      = config.window_height,
    .window_title       = config.app_name,
  };
  surface_ = vk_surface_create(&surface_config);
  if (!surface_)
    return false;  // NOTE: error message already sent to stderr.

  VkSurfaceKHR      window_surface = vk_surface_get_surface_khr(surface_);
  VkImageUsageFlags image_usage    = 0;

  // Check that rendering directly to the swapchain is supported

  if (config.require_swapchain_image_shader_storage)
    image_usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  if (config.require_swapchain_transfers)
    image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  VkFormat wanted_format = config.wanted_format;

  const vk_swapchain_config_t swapchain_config = {
    .instance        = device_->vk_instance(),
    .device          = device_->vk_device(),
    .physical_device = device_->vk_physical_device(),
    .allocator       = device_->vk_allocator(),

    .present_queue_family = device_->graphics_queue_family(),
    .present_queue_index  = 0,

    .surface_khr = window_surface,

    .max_frames        = 3,
    .pixel_format      = wanted_format,
    .disable_vsync     = config.disable_vsync,
    .image_usage_flags = image_usage,
    .staging_mode      = config.require_swapchain_image_shader_storage
                      ? VK_SWAPCHAIN_STAGING_MODE_IF_NEEDED
                      : VK_SWAPCHAIN_STAGING_MODE_NONE,
  };
  swapchain_ = vk_swapchain_create(&swapchain_config);

  // Sanity check.
  VkSurfaceFormatKHR surface_format = vk_swapchain_get_format(swapchain_);
  if (wanted_format != VK_FORMAT_UNDEFINED && surface_format.format != wanted_format)
    {
      fprintf(stderr, "WARNING: Could not find wanted pixel format, colors may be wrong!\n");
    }

  if (config.verbose)
    vk_swapchain_print(swapchain_);

  info_.image_count    = vk_swapchain_get_image_count(swapchain_);
  info_.extent         = vk_swapchain_get_extent(swapchain_);
  info_.surface        = window_surface;
  info_.surface_format = surface_format;

  if (config.enable_swapchain_queue)
    {
      vk_swapchain_queue_config_t queue_config = {
        .swapchain    = swapchain_,
        .queue_family = device_->graphics_queue_family(),
        .queue_index  = 0u,
        .device       = device_->vk_device(),
        .allocator    = device_->vk_allocator(),

        .enable_framebuffers   = config.enable_framebuffers,
        .sync_semaphores_count = config.sync_semaphores_count,
      };
      swapchain_queue_ = vk_swapchain_queue_create(&queue_config);
    }

  return true;
}

VulkanWindow::~VulkanWindow()
{
  if (swapchain_queue_)
    vk_swapchain_queue_destroy(swapchain_queue_);

  if (swapchain_)
    vk_swapchain_destroy(swapchain_);

  if (surface_)
    vk_surface_destroy(surface_);
}

bool
VulkanWindow::acquireSwapchainImage()
{
  ASSERT_MSG(!swapchain_queue_, "Calling this method requires enable_swapchain_queue=false");
  return vk_swapchain_acquire_next_image(swapchain_, &image_index_);
}

void
VulkanWindow::presentSwapchainImage()
{
  ASSERT_MSG(!swapchain_queue_, "Calling this method requires enable_swapchain_queue=false");
  vk_swapchain_present_image(swapchain_);
}

bool
VulkanWindow::acquireSwapchainQueueImage()
{
  ASSERT_MSG(swapchain_queue_, "Calling this method requires enable_swapchain_queue=true");

  swapchain_queue_image_ = vk_swapchain_queue_acquire_next_image(swapchain_queue_);
  if (!swapchain_queue_image_)
    return false;

  image_index_ = vk_swapchain_queue_get_index(swapchain_queue_);
  return true;
}

void
VulkanWindow::presentSwapchainQueueImage()
{
  ASSERT_MSG(swapchain_queue_, "Calling this method requires enable_swapchain_queue=true");
  vk_swapchain_queue_submit_and_present_image(swapchain_queue_);
}

bool
VulkanWindow::handleUserEvents()
{
  return vk_surface_poll_events(surface_);
}

void
VulkanWindow::waitIdle()
{
  vkDeviceWaitIdle(device_->vk_device());
}
