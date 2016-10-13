// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_allocator.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

GpuAllocator::GpuAllocator(const VulkanContext& context)
    : physical_device_(context.physical_device), device_(context.device) {}

GpuAllocator::~GpuAllocator() {
  FTL_CHECK(num_bytes_allocated_ == 0);
}

GpuMem GpuAllocator::Allocate(vk::MemoryRequirements reqs,
                              vk::MemoryPropertyFlags flags) {
  vk::MemoryAllocateInfo info;
  info.allocationSize = reqs.size;
  // TODO: cache flags for efficiency?
  info.memoryTypeIndex =
      GetMemoryTypeIndex(physical_device_, reqs.memoryTypeBits, flags);

  // TODO: allocate large chunks of memory, and return memory from within these
  // chunks.  This will require changes to Free(), of course.
  vk::DeviceMemory mem = ESCHER_CHECKED_VK_RESULT(device_.allocateMemory(info));
  num_bytes_allocated_ += reqs.size;
  // TODO: need to manually overallocate and adjust offset to ensure alignment,
  // based on the content of reqs.alignment?
  return GpuMem(mem, 0, reqs.size, info.memoryTypeIndex, this);
}

void GpuAllocator::Free(GpuMem mem) {
  FTL_CHECK(mem.offset() == 0);
  num_bytes_allocated_ -= mem.size();
  device_.freeMemory(mem.base());
}

}  // namespace impl
}  // namespace escher
