// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo_vulkan_app.h"

#include <stdio.h>

#include "tests/common/utils.h"
#include "tests/common/vk_surface.h"

bool
DemoVulkanApp::init(const DemoVulkanApp::Config & config, AppStateConfigCallback * config_callback)
{
  vk_app_state_config_t app_config = {
    .app_name            = config.app_name ? config.app_name : "DemoVulkanApp",
    .engine_name         = "DemoVulkanApp",
    .enable_validation   = config.debug,
    .enable_debug_report = config.debug,

    .require_swapchain         = true,
    .disable_swapchain_present = config.disable_vsync,
  };

  if (config_callback)
    (*config_callback)(&app_config);

  if (!vk_app_state_init(&app_state_, &app_config))
    {
      fprintf(stderr, "FAILURE\n");
      return false;
    }

  if (config.verbose)
    vk_app_state_print(&app_state_);

  VkInstance instance = app_state_.instance;

  vkGetDeviceQueue(app_state_.d, app_state_.qfi, 0, &graphics_queue_);

  // Allocate display surface, and determine whether it's possible to directly
  // render to the swapchain with it.
  swapchain_surface_ =
    vk_app_state_create_surface(&app_state_, config.window_width, config.window_height);

  VkImageUsageFlags image_usage = 0;

  // Check that rendering directly to the swapchain is supported
  if (config.require_swapchain_image_shader_storage)
    {
      vk_device_surface_info_t surface_info;
      vk_device_surface_info_init(&surface_info,
                                  app_state_.pd,
                                  swapchain_surface_,
                                  app_state_.instance);

      VkFormat format = vk_device_surface_info_find_presentation_format(&surface_info,
                                                                        VK_IMAGE_USAGE_STORAGE_BIT,
                                                                        VK_FORMAT_UNDEFINED);
      vk_device_surface_info_destroy(&surface_info);

      if (format == VK_FORMAT_UNDEFINED)
        {
          fprintf(stderr, "ERROR: display surface does not support VK_IMAGE_USAGE_STORAGE_BIT!\n");
          return false;
        }

      image_usage = VK_IMAGE_USAGE_STORAGE_BIT;
    }

  VkFormat wanted_format = config.wanted_format;

  const vk_swapchain_config_t swapchain_config = {
    .instance        = instance,
    .device          = app_state_.d,
    .physical_device = app_state_.pd,
    .allocator       = app_state_.ac,

    .present_queue_family  = app_state_.qfi,
    .present_queue_index   = 0,
    .graphics_queue_family = app_state_.qfi,
    .graphics_queue_index  = 0,

    .surface_khr = swapchain_surface_,

    .max_frames              = 3,
    .pixel_format            = wanted_format,
    .disable_vsync           = config.disable_vsync,
    .image_usage_flags       = image_usage,
    .use_presentation_layout = true,
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

  swapchain_image_count_ = vk_swapchain_get_image_count(swapchain_);
  swapchain_extent_      = vk_swapchain_get_extent(swapchain_);

  print_fps_   = config.print_fps;
  print_ticks_ = config.debug;

  if (config.enable_swapchain_queue)
    {
      vk_swapchain_queue_config_t queue_config = {
        .swapchain    = swapchain_,
        .queue_family = app_state_.qfi,
        .queue_index  = 0u,
        .device       = app_state_.d,
        .allocator    = app_state_.ac,

        .enable_framebuffers   = config.enable_framebuffers,
        .sync_semaphores_count = config.sync_semaphores_count,
      };
      swapchain_queue_ = vk_swapchain_queue_create(&queue_config);
    }
  return true;
}

DemoVulkanApp::~DemoVulkanApp()
{
  if (swapchain_)
    vk_swapchain_destroy(swapchain_);

  vk_app_state_destroy(&app_state_);
}

void
DemoVulkanApp::run()
{
  if (!this->setup())
    return;

  if (print_fps_)
    fps_counter_start(&fps_counter_);

  uint32_t frame_counter = 0;
  while (!do_quit_ && vk_app_state_poll_events(&app_state_))
    {
      if (!drawFrame(frame_counter))
        break;

      if (print_fps_)
        fps_counter_tick_and_print(&fps_counter_);

      // With --debug, print a small tick every two seconds (assuming a 60hz
      // swapchain) to check that everything is working.
      if (print_ticks_)
        {
          if (frame_counter > 0 && frame_counter % (60 * 2) == 0)
            {
              printf("!");
              fflush(stdout);
            }
        }

      frame_counter++;
    }

  if (print_fps_)
    fps_counter_stop(&fps_counter_);

  vkDeviceWaitIdle(app_state_.d);
  this->teardown();
}

void
DemoVulkanApp::doQuit()
{
  do_quit_ = true;
}

bool
DemoVulkanApp::acquireSwapchainImage()
{
  ASSERT_MSG(!swapchain_queue_, "Calling this method requires enable_swapchain_queue=false");
  return vk_swapchain_acquire_next_image(swapchain_, &image_index_);
}

void
DemoVulkanApp::presentSwapchainImage()
{
  ASSERT_MSG(!swapchain_queue_, "Calling this method requires enable_swapchain_queue=false");
  vk_swapchain_present_image(swapchain_);
}

bool
DemoVulkanApp::acquireSwapchainQueueImage()
{
  ASSERT_MSG(swapchain_queue_, "Calling this method requires enable_swapchain_queue=true");

  swapchain_queue_image_ = vk_swapchain_queue_acquire_next_image(swapchain_queue_);
  if (!swapchain_queue_image_)
    return false;

  image_index_ = vk_swapchain_queue_get_index(swapchain_queue_);
  return true;
}

void
DemoVulkanApp::presentSwapchainQueueImage()
{
  ASSERT_MSG(swapchain_queue_, "Calling this method requires enable_swapchain_queue=true");
  vk_swapchain_queue_submit_and_present_image(swapchain_queue_);
}
