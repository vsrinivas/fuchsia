// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXT_SPINEL_VK_FIND_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXT_SPINEL_VK_FIND_TARGET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct spn_vk_target;
struct hotsort_vk_target;

// Find the best device-specific pair of Spinel and Hotsort targets that
// correspond to a given Vulkan |vendor_id| and |device_id| pair.
// On success, returns true and sets |*spinel_target| and |*hotsort_target|.
// On failure, fill |error_buffer| with a 0-terminated human friendly error
// message explaining the issue.
//
// Note: The spinel and hotsort targets returned on success should be copied
//       into an spn_vk_create_info struct by the application before calling
//       spn_vk_create_context().
bool
spn_vk_find_target(uint32_t const                          vendor_id,
                   uint32_t const                          device_id,
                   struct spn_vk_target const ** const     spinel_target,
                   struct hotsort_vk_target const ** const hotsort_target,
                   char * const                            error_buffer,
                   size_t const                            error_buffer_size);

//
//
//

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXT_SPINEL_VK_FIND_TARGET_H_
