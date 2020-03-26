// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/third_party/granite/vk/descriptor_set_layout.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

namespace {
using namespace escher;

TEST(DescriptorSetLayout, Validate) {
  impl::DescriptorSetLayout original_layout = {};
  original_layout.sampled_image_mask = 1 << 0U;
  original_layout.storage_image_mask = 1 << 1U;
  original_layout.uniform_buffer_mask = 1 << 2U;
  original_layout.storage_buffer_mask = 1 << 3U;
  original_layout.sampled_buffer_mask = 1 << 4U;
  original_layout.input_attachment_mask = 1 << 5U;
  original_layout.fp_mask = 0U;

  impl::DescriptorSetLayout layout;
  uint32_t* integers = &layout.sampled_image_mask;

  // Having the same bit appear in two of the masks results in a validation
  // failure (because this corresponds to 2 descriptors in the set with the
  // same binding index).
  FXL_LOG(INFO) << "==== NOTE: validation warnings are expected";
  for (size_t type_index = 0; type_index <= 5U; ++type_index) {
    layout = original_layout;
    EXPECT_TRUE(layout.IsValid());
    integers[type_index] |= 1 << ((type_index + 1) % 5U);
    EXPECT_FALSE(layout.IsValid());
  }
  FXL_LOG(INFO) << "==== NOTE: no additional validation warnings are expected";

  // No problem to add an additional binding anywhere else, of any type.
  for (size_t bit_index = 6U; bit_index < VulkanLimits::kNumBindings; ++bit_index) {
    for (size_t type_index = 0; type_index <= 5U; ++type_index) {
      layout = original_layout;
      integers[type_index] |= 1 << bit_index;
      EXPECT_TRUE(layout.IsValid());
    }
  }
}

}  // namespace
