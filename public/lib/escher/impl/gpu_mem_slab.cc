// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/gpu_mem_slab.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/vk/gpu_allocator.h"
#include "lib/fxl/logging.h"

namespace escher {
namespace impl {

GpuMemSlab::GpuMemSlab(vk::Device device, vk::DeviceMemory base,
                       vk::DeviceSize size, uint8_t* mapped_ptr,
                       uint32_t memory_type_index, GpuAllocator* allocator)
    : GpuMem(base, size, 0, mapped_ptr),
      device_(device),
      memory_type_index_(memory_type_index),
      allocator_(allocator) {
  if (allocator_) {
    allocator_->OnSlabCreated(size);
  }
}

GpuMemSlabPtr GpuMemSlab::New(vk::Device device,
                              vk::PhysicalDevice physical_device,
                              vk::MemoryRequirements reqs,
                              vk::MemoryPropertyFlags flags,
                              GpuAllocator* allocator) {
  vk::DeviceMemory vk_mem;
  uint8_t* mapped_ptr = nullptr;
  uint32_t memory_type_index = 0;
  if (!device) {
    // To support testing, we allow a null device, and respond by creating
    // a GpuMemSlab with a null vk::DeviceMemory.
  } else {
    // Determine whether we will need to map the memory.
    bool needs_mapped_ptr = false;
    if (flags & vk::MemoryPropertyFlagBits::eHostVisible) {
      // We don't currently provide an interface for flushing mapped data, so
      // ensure that the allocated memory is cache-coherent.  This is more
      // convenient anyway.
      flags |= vk::MemoryPropertyFlagBits::eHostCoherent;
      needs_mapped_ptr = true;
    }

    // TODO: cache flags for efficiency? Or perhaps change signature of this
    // method to directly take the memory-type index.
    memory_type_index =
        GetMemoryTypeIndex(physical_device, reqs.memoryTypeBits, flags);

    vk::MemoryAllocateInfo info;
    info.allocationSize = reqs.size;
    info.memoryTypeIndex = memory_type_index;
    vk_mem = ESCHER_CHECKED_VK_RESULT(device.allocateMemory(info));

    if (needs_mapped_ptr) {
      auto ptr =
          ESCHER_CHECKED_VK_RESULT(device.mapMemory(vk_mem, 0, reqs.size));
      mapped_ptr = reinterpret_cast<uint8_t*>(ptr);
    }
  }

  return fxl::AdoptRef(new GpuMemSlab(device, vk_mem, reqs.size, mapped_ptr,
                                      memory_type_index, allocator));
}

GpuMemSlab::~GpuMemSlab() {
  if (device_ && base()) {
    if (mapped_ptr()) {
      device_.unmapMemory(base());
    }
    device_.freeMemory(base());
  }
  if (allocator_) {
    allocator_->OnSlabDestroyed(size());
  }
}

void GpuMemSlab::OnAllocationDestroyed(vk::DeviceSize size,
                                       vk::DeviceSize offset) {
  if (allocator_) {
    allocator_->OnSuballocationDestroyed(this, size, offset);
  }
}

}  // namespace impl
}  // namespace escher
