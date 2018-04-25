// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "lib/escher/util/debug_print.h"

namespace escher {

// The fields are bitmaps where each bit corresponds to a binding index within
// the set.  Therefore, there can be at most 32 descriptor bindings per set.
// Note that a bit can only be set in one of the fields; in other words for
// any pair of masks, (mask1 & mask2) must equal zero.  |fp_mask| is the
// exception; it tracks whether image formats are floating point or not.
struct DescriptorSetLayout {
  uint32_t sampled_image_mask = 0;
  uint32_t storage_image_mask = 0;
  uint32_t uniform_buffer_mask = 0;
  uint32_t storage_buffer_mask = 0;
  uint32_t sampled_buffer_mask = 0;
  uint32_t input_attachment_mask = 0;
  uint32_t fp_mask = 0;
  vk::ShaderStageFlags stages;
};
ESCHER_DEBUG_PRINTABLE(DescriptorSetLayout);

}  // namespace escher
