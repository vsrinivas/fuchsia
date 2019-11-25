// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_DEVICE_CONFIG_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_DEVICE_CONFIG_UTILS_H_

#include <vulkan/vulkan.h>

#include "tests/common/vk_app_state.h"

#ifdef __cplusplus
extern "C"
#endif

  //
  //
  //

  struct spn_vk_target;
struct hotsort_vk_target;

// A small structure describing Spinel and hotsort device configuration.
// This is filled by vk_spinel_device_config_callback.
// |spinel_target| and |hotsort_target| can later be passed to
// spn_vk_context_create().
typedef struct
{
  // The following are read by vk_spinel_device_config_callback().
  uint32_t wanted_vendor_id;  // If not 0, device must match the value exactly.
  uint32_t wanted_device_id;  // If this and vendor ID are not 0, device must match exactly.

  // The following are filled by vk_spinel_device_config_callback().
  const struct spn_vk_target *     spinel_target;
  const struct hotsort_vk_target * hotsort_target;

} vk_spinel_device_configuration_t;

// This function is used to select a Vulkan device configuration based on
// Spinel (and Hotsort) target requirements. Usage is:
//
//  vk_spinel_device_configuration_t  spinel_device_config = {};
//
//  const vk_app_state_config_t app_config = {
//     ...
//     .device_config_callback = vk_spinel_device_config_callback,
//     .device_config_opaque = &spinel_device_config,
//  };
//
//  vk_app_state_t app;;
//  if (!vk_app_state_init(&app, &app_config)) {
//    return EXIT_FAILURE;
//  }
//
//  struct spn_environment environment = vk_app_state_get_spinel_environment(&app);
//  spn_context_t          context;
//
//  spn_vk_context_create(
//      &environment,
//      &(const struct spn_vk_context_create_info){
//        .spinel          = spinel_device_config.spinel_target,
//        .hotsort         = spinel_device_config.hotsort_target,
//        .block_pool_size = 1 << 26,  // 64 MB (128K x 128-dword blocks)
//        .handle_count    = 1 << 15,  // 32K handles
//      },
//      &context);
//
extern bool
vk_spinel_device_config_callback(void *               opaque,
                                 VkInstance           instance,
                                 VkPhysicalDevice     device,
                                 vk_device_config_t * device_config);

// Return an spn_environment struct initialized from a vk_app_state_t instance.
extern struct spn_vk_environment
vk_app_state_get_spinel_environment(const vk_app_state_t * app_state);

extern void
spn_vk_environment_print(const struct spn_vk_environment * environment);

//
//
//

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_DEVICE_CONFIG_UTILS_H_
