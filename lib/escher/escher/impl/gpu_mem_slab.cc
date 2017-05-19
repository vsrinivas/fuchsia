// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_mem_slab.h"

#include "escher/impl/gpu_allocator.h"
#include "escher/impl/vulkan_utils.h"
#include "ftl/logging.h"

namespace escher {
namespace impl {

GpuMemSlab::GpuMemSlab(vk::Device device,
                       vk::DeviceMemory base,
                       vk::DeviceSize size,
                       uint32_t memory_type_index,
                       GpuAllocator* allocator)
    : GpuMem(base, size),
      device_(device),
      memory_type_index_(memory_type_index),
      allocator_(allocator) {
  if (allocator_) {
    allocator_->num_bytes_allocated_ += size;
    allocator_->slab_count_++;
  }
}

GpuMemSlabPtr GpuMemSlab::New(vk::Device device,
                              vk::PhysicalDevice physical_device,
                              vk::MemoryRequirements reqs,
                              vk::MemoryPropertyFlags flags,
                              GpuAllocator* allocator) {
  vk::DeviceMemory mem;
  uint32_t memory_type_index = 0;
  if (!device) {
    // To support testing, we allow a null device, and respond by creating
    // a GpuMemSlab with a null vk::DeviceMemory.
  } else {
    // TODO: cache flags for efficiency? Or perhaps change signature of this
    // method to directly take the memory-type index.
    memory_type_index =
        GetMemoryTypeIndex(physical_device, reqs.memoryTypeBits, flags);

    vk::MemoryAllocateInfo info;
    info.allocationSize = reqs.size;
    info.memoryTypeIndex = memory_type_index;
    mem = ESCHER_CHECKED_VK_RESULT(device.allocateMemory(info));
  }
  return ftl::AdoptRef(
      new GpuMemSlab(device, mem, reqs.size, memory_type_index, allocator));
}

GpuMemSlab::~GpuMemSlab() {
  if (device_ && base()) {
    device_.freeMemory(base());
  }
  if (allocator_) {
    allocator_->num_bytes_allocated_ -= size();
    allocator_->slab_count_--;
  }
}

}  // namespace impl
}  // namespace escher
