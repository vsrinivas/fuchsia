// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_allocator.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

GpuAllocator::GpuAllocator(const VulkanContext& context)
    : physical_device_(context.physical_device),
      device_(context.device),
      num_bytes_allocated_(0),
      slab_count_(0) {}

GpuAllocator::~GpuAllocator() {
  FTL_CHECK(num_bytes_allocated_ == 0);
  FTL_CHECK(slab_count_ == 0);
}

GpuMemSlabPtr GpuAllocator::AllocateSlab(vk::MemoryRequirements reqs,
                                         vk::MemoryPropertyFlags flags) {
  return GpuMemSlab::New(device(), physical_device(), reqs, flags, this);
}

}  // namespace impl
}  // namespace escher
