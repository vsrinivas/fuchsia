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

VkResult
vk_pipeline_cache_create(VkDevice                            device,
                         VkAllocationCallbacks const *       allocator,
                         char                  const * const name,
                         VkPipelineCache             *       pipeline_cache);

VkResult
vk_pipeline_cache_destroy(VkDevice                            device,
                          VkAllocationCallbacks const *       allocator,
                          char                  const * const name,
                          VkPipelineCache                     pipeline_cache);

//
//
//
