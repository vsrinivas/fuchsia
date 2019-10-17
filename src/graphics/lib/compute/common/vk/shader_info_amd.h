// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_SHADER_INFO_AMD_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_SHADER_INFO_AMD_H_

//
//
//

#include <stdbool.h>
#include <vulkan/vulkan.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

//
// Returns true if AMD shader info extension was enabled.
//
// This is false by default unless
// vk_shader_info_statistics_set_enabled(true) is called.
//

bool
vk_shader_info_amd_statistics_is_enabled(void);

//
// Enable AMD shader info extension.
//
// Should be called when the corresponding device extension was enabled
// when creating a Vulkan instance.
//

void
vk_shader_info_amd_statistics_enable(void);

//
// Print AMD-specific shader statistics.
//
// Doesn't do anything if the feature was not enabled. Otherwise:
//   * |device| is the Vulkan device handle.
//   * |p| is an array of |count| VkPipeline handle.
//   * |names| is an array of |count| strings naming each pipeline for
//     output.
//

void
vk_shader_info_amd_statistics(VkDevice           device,
                              VkPipeline         p[],
                              char const * const names[],
                              uint32_t const     count);

//
// Print AMD-specific shader disassembly.
//
// Doesn't do anything if the feature was not enabled. Otherwise:
//   * |device| is the Vulkan device handle.
//   * |p| is an array of |count| VkPipeline handle.
//   * |names| is an array of |count| strings naming each pipeline for
//     output.
//

void
vk_shader_info_amd_disassembly(VkDevice           device,
                               VkPipeline         p[],
                               char const * const names[],
                               uint32_t const     count);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_SHADER_INFO_AMD_H_
