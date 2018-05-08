// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_DESCRIPTOR_SET_LAYOUT_H_
#define LIB_ESCHER_VK_DESCRIPTOR_SET_LAYOUT_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/util/debug_print.h"

namespace escher {

// The fields are bitmaps where each bit corresponds to a binding index within
// the set.  Therefore, there can be at most 32 descriptor bindings per set.
// Note that a bit can only be set in one of the fields; in other words for
// any pair of masks, (mask1 & mask2) must equal zero.  |fp_mask| is the
// exception; it tracks whether image formats are floating point or not.
//
// NOTE: When interpreting DescriptorSetLayouts, various code assumes that
// uniform buffers use dynamic offsets and storage buffers use static offsets.
//
// TODO(SCN-699): Consider allowing both static/dynamic offsets for both storage
// and uniform buffers.
struct DescriptorSetLayout {
  uint32_t sampled_image_mask = 0;
  uint32_t storage_image_mask = 0;
  uint32_t uniform_buffer_mask = 0;
  uint32_t storage_buffer_mask = 0;
  uint32_t sampled_buffer_mask = 0;
  uint32_t input_attachment_mask = 0;
  uint32_t fp_mask = 0;
  vk::ShaderStageFlags stages;

  bool operator==(const DescriptorSetLayout& other) const {
    return sampled_image_mask == other.sampled_image_mask &&
           storage_image_mask == other.storage_image_mask &&
           uniform_buffer_mask == other.uniform_buffer_mask &&
           storage_buffer_mask == other.storage_buffer_mask &&
           sampled_buffer_mask == other.sampled_buffer_mask &&
           input_attachment_mask == other.input_attachment_mask &&
           fp_mask == other.fp_mask && stages == other.stages;
  }

  // Return false if the layout is invalid in any way (multiple descriptors
  // sharing the same binding index, etc).
  bool IsValid();
};

ESCHER_DEBUG_PRINTABLE(DescriptorSetLayout);

}  // namespace escher

#endif  // LIB_ESCHER_VK_DESCRIPTOR_SET_LAYOUT_H_
