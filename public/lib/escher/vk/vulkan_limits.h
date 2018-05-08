// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_VULKAN_LIMITS_H_
#define LIB_ESCHER_VK_VULKAN_LIMITS_H_

#include <cstdint>

namespace escher {

// These are limits imposed by Escher, and are constexpr so that they can be
// used to statically declare array sizes.  The actual Vulkan limits vary from
// device to device, and must be queried dynamically.
struct VulkanLimits {
  static constexpr uint64_t kNumAttachments = 8;
  static constexpr uint64_t kNumBindings = 16;
  static constexpr uint64_t kNumDescriptorSets = 4;
  static constexpr uint64_t kNumVertexAttributes = 16;
  static constexpr uint64_t kNumVertexBuffers = 4;
  static constexpr uint64_t kPushConstantSize = 128;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_VULKAN_LIMITS_H_
