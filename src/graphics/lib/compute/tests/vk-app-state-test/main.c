// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "tests/common/vk_app_state.h"

//
// A tiny test to check that vk_app_state_t create/destruction works properly
// without swapchain support enabled.
//

int
main(int argc, char const * argv[])
{
  vk_app_state_config_t app_config = {
    .app_name              = "vk_app_state_test",
    .enable_validation     = true,
    .enable_debug_report   = true,
    .enable_amd_statistics = true,
    .device_config = {
      .required_queues = VK_QUEUE_GRAPHICS_BIT,
      .vendor_id       = (argc <= 2) ? 0 : strtoul(argv[1], NULL, 16),
      .device_id       = (argc <= 3) ? 0 : strtoul(argv[2], NULL, 16),
    },
  };

  vk_app_state_t app_state = {};

  if (!vk_app_state_init(&app_state, &app_config))
    return EXIT_FAILURE;

  vk_app_state_print(&app_state);

  //
  // dispose of Vulkan resources
  //
  vk_app_state_destroy(&app_state);

  return EXIT_SUCCESS;
}

//
//
//
