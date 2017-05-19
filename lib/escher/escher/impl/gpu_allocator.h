// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/impl/gpu_mem_slab.h"
#include "escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

// Vulkan does not support large numbers of memory allocations.  Instead,
// applications are expected to allocate larger chunks of memory, and do their
// own memory management within these chunks.  This is the responsibility of
// concrete subclasses of GpuAllocator.
// TODO: move out of impl namespace.
class GpuAllocator {
 public:
  GpuAllocator(const VulkanContext& context);
  virtual ~GpuAllocator();

  virtual GpuMemPtr Allocate(vk::MemoryRequirements reqs,
                             vk::MemoryPropertyFlags flags) = 0;

  // Current number of bytes allocated (i.e. unfreed) by this allocator.
  uint64_t GetNumBytesAllocated() { return num_bytes_allocated_; }

  vk::PhysicalDevice physical_device() const { return physical_device_; }
  vk::Device device() const { return device_; }
  uint32_t slab_count() const { return slab_count_; }

 protected:
  // Concrete subclasses use this to allocate GpuMemSlabs that are then used
  // to suballocate GpuMem instances from.
  GpuMemSlabPtr AllocateSlab(vk::MemoryRequirements reqs,
                             vk::MemoryPropertyFlags flags);

 private:
  friend class GpuMemSlab;
  vk::PhysicalDevice physical_device_;
  vk::Device device_;
  vk::DeviceSize num_bytes_allocated_;

  mutable std::atomic_uint_fast32_t slab_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuAllocator);
};

}  // namespace impl
}  // namespace escher
