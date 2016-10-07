// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_allocator.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

GpuMem::GpuMem(vk::DeviceMemory base,
               vk::DeviceSize offset,
               vk::DeviceSize size,
               uint32_t memory_type_index,
               GpuAllocator* allocator)
    : base_(base),
      offset_(offset),
      size_(size),
      memory_type_index_(memory_type_index),
      allocator_(allocator) {}

GpuMem::GpuMem(GpuMem&& other)
    : base_(other.base_),
      offset_(other.offset_),
      size_(other.size_),
      memory_type_index_(other.memory_type_index_),
      allocator_(other.allocator_) {
  other.base_ = 0;
  other.offset_ = 0;
  other.size_ = 0;
  other.memory_type_index_ = UINT32_MAX;
  other.allocator_ = nullptr;
}

GpuMem::~GpuMem() {
  // If this object has been moved, then destruction is a no-op.
  if (allocator_) {
    GpuAllocator* saved = allocator_;
    allocator_ = nullptr;
    saved->Free(std::move(*this));
  }
}

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
