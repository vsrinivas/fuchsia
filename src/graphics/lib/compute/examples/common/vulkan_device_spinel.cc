// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/spinel_vk/spinel_vk_device_config_utils.h"
#include "vulkan_device.h"

bool
VulkanDevice::initForSpinel(const VulkanDevice::Config & config,
                            uint32_t                     vendor_id,
                            uint32_t                     device_id,
                            spn_vk_context_create_info * create_info)
{
  spinel_vk_device_configuration_t spinel_device_config = {
    .wanted_vendor_id = vendor_id,
    .wanted_device_id = device_id,
  };

  VulkanDevice::AppStateConfigCallback config_callback =
    [&spinel_device_config](vk_app_state_config_t * config) {
      config->device_config_callback = spinel_vk_device_config_callback;
      config->device_config_opaque   = &spinel_device_config;
      config->enable_pipeline_cache  = true;
    };

  if (!init(config, &config_callback))
    return false;

  // NOTE: The |block_pool_size| and |handle_count| values here are just
  // defaults that might be overwritten by the caller after this call.
  *create_info = (spn_vk_context_create_info){
    .spinel          = spinel_device_config.spinel_target,
    .hotsort         = spinel_device_config.hotsort_target,
    .block_pool_size = 1 << 26,
    .handle_count    = 1 << 15,
  };

  return true;
}
