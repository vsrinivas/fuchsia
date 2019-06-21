// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_fixed_functions.h"

VulkanFixedFunctions::VulkanFixedFunctions(const VkExtent2D &extent)
    : extent_(extent) {
  scissor_ = {
      .extent = extent_,
  };

  viewport_ = {
      .height = static_cast<float>(extent_.height),
      .width = static_cast<float>(extent_.width),
  };
};
