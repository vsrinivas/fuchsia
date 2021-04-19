// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_EXT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_EXT_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Radix sort extensions
// ---------------------
//
#ifndef RADIX_SORT_VK_DISABLE_EXTENSIONS

//
// Extension types
//
enum radix_sort_vk_ext_type
{
  RADIX_SORT_VK_EXT_TIMESTAMPS
};

//
// Timestamp each logical step of the algorithm
//
// Number of timestamps is: 4 + (number of subpasses)
//
struct radix_sort_vk_ext_timestamps
{
  void *                      ext;
  enum radix_sort_vk_ext_type type;
  uint32_t                    timestamp_count;
  VkQueryPool                 timestamps;
  uint32_t                    timestamps_set;
};

#endif

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_EXT_H_
