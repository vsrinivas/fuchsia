// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan.h>

//
//
//

void
vk_shader_info_amd_statistics(VkDevice           device,
                              VkPipeline         p[],
                              char const * const names[],
                              uint32_t     const count);

void
vk_shader_info_amd_disassembly(VkDevice           device,
                               VkPipeline         p[],
                               char const * const names[],
                               uint32_t     const count);

//
//
//
