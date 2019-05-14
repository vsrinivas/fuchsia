// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_SHADER_INFO_AMD_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_SHADER_INFO_AMD_H_

//
//
//

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

void
vk_shader_info_amd_statistics(VkDevice           device,
                              VkPipeline         p[],
                              char const * const names[],
                              uint32_t const     count);

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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_SHADER_INFO_AMD_H_
