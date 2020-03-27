// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo_app_base.h"

#include <stdio.h>

#include "tests/common/utils.h"
#include "tests/common/vk_device_surface_info.h"

bool
DemoAppBase::init(const DemoAppBase::Config & config)
{
  VulkanDevice::Config dev_config = {
    .app_name          = config.app_name,
    .verbose           = config.verbose,
    .debug             = config.debug,
    .require_swapchain = true,
    .disable_vsync     = config.disable_vsync,
  };
  ASSERT_MSG(device_.init(dev_config), "Could not initialize Vulkan device!\n");
  return initAfterDevice(config);
}

bool
DemoAppBase::initAfterDevice(const DemoAppBase::Config & config)
{
  // Allocate display surface, and determine whether it's possible to directly
  // render to the swapchain with it.
  VulkanWindow::Config win_config = {
    .app_name      = config.app_name ? config.app_name : "DemoAppBase",
    .window_width  = config.window_width,
    .window_height = config.window_height,
    .verbose       = config.verbose,
    .debug         = config.debug,
    .disable_vsync = config.disable_vsync,
    .wanted_format = config.wanted_format,

    .require_swapchain_image_shader_storage = config.require_swapchain_image_shader_storage,
    .require_swapchain_transfers            = config.require_swapchain_transfers,
  };
  if (config.enable_swapchain_queue)
    {
      win_config.enable_swapchain_queue = true;
      win_config.enable_framebuffers    = config.enable_framebuffers;
      win_config.sync_semaphores_count  = config.sync_semaphores_count;
    }

  ASSERT_MSG(window_.init(&device_, win_config), "Could not initialize display surface");

  swapchain_             = window_.swapchain();
  swapchain_image_count_ = window_.info().image_count;

  print_fps_   = config.print_fps;
  print_ticks_ = config.debug;
  return true;
}

DemoAppBase::~DemoAppBase()
{
}

void
DemoAppBase::run()
{
  if (!this->setup())
    return;

  if (print_fps_)
    fps_counter_start(&fps_counter_);

  uint32_t frame_counter = 0;
  while (!do_quit_ && window_.handleUserEvents())
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

  window_.waitIdle();
  this->teardown();
}

void
DemoAppBase::doQuit()
{
  do_quit_ = true;
}
