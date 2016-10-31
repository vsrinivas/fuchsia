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

std::unique_ptr<GpuMemSlab> GpuAllocator::AllocateSlab(
    vk::DeviceSize size,
    uint32_t memory_type_index) {
  vk::MemoryAllocateInfo info;
  info.allocationSize = size;
  info.memoryTypeIndex = memory_type_index;
  vk::DeviceMemory mem = ESCHER_CHECKED_VK_RESULT(device_.allocateMemory(info));
  num_bytes_allocated_ += size;
  ++slab_count_;
  return std::unique_ptr<GpuMemSlab>(
      new GpuMemSlab(mem, size, memory_type_index, this));
}

void GpuAllocator::FreeSlab(std::unique_ptr<GpuMemSlab> slab) {
  FTL_DCHECK(slab->ref_count_ == 0);
  FTL_DCHECK(slab->allocator_ == this);
  num_bytes_allocated_ -= slab->size();
  device_.freeMemory(slab->base());
  --slab_count_;
}

GpuMemPtr GpuAllocator::AllocateMem(GpuMemSlab* slab,
                                    vk::DeviceSize offset,
                                    vk::DeviceSize size) {
  FTL_DCHECK(slab->allocator_ == this);
  FTL_DCHECK(offset + size <= slab->size());
  return ftl::AdoptRef(new GpuMem(slab, offset, size));
}

}  // namespace impl
}  // namespace escher
