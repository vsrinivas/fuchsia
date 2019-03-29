// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>
#include <stdbool.h>

//
//
//

uint32_t
vk_find_mem_type_idx(VkPhysicalDeviceMemoryProperties const * pdmp,
                     uint32_t                                 memoryTypeBits,
                     VkMemoryPropertyFlags            const   mpf);

//
//
//
