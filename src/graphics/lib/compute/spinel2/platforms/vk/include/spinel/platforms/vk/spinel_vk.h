// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_SPINEL_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_SPINEL_VK_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel/spinel.h"
#include "spinel_vk_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// SPINEL REQUIREMENTS
//
// Get a Spinel target's Vulkan requirements.
//
// A Spinel target is a binary image containing configuration parameters and a
// bundle of SPIR-V modules.
//
// Targets are prebuilt and specific to a particular device vendor, architecture
// and key-val configuration.
//
// A Spinel context can only be created with a VkDevice that is initialized with
// all of the target's required extensions and features.
//
// The `spinel_vk_target_get_requirements()` function yields the extensions and
// initialized feature flags required by a Spinel target.
//
// These requirements can be merged with other Vulkan library requirements
// before VkDevice creation.
//
// If the `.ext_names` member is NULL, the `.ext_name_count` member will be
// initialized.
//
// Returns `false` if:
//
//   * The .ext_names field is NULL and the number of required extensions is
//     greater than zero.
//   * The .ext_name_count is less than the number of required extensions is
//     greater than zero.
//   * Any of the .pdf, .pdf11 or .pdf12 members are NULL.
//
// Otherwise, returns true.
//
bool
spinel_vk_target_get_requirements(spinel_vk_target_t const *        target,
                                  spinel_vk_target_requirements_t * requirements);

//
// SPINEL CONTEXT CREATION
//
// Every value in the `spinel_vk_context_create_info_t` structure must be
// initialized.
//
// Returns NULL upon failure.
//
spinel_context_t
spinel_vk_context_create(struct spinel_vk_context_create_info const * create_info);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_SPINEL_VK_H_
