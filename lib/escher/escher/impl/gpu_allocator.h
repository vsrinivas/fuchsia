// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/impl/gpu_mem.h"
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

  uint64_t GetNumBytesAllocated() { return num_bytes_allocated_; }

  vk::PhysicalDevice physical_device() { return physical_device_; }
  vk::Device device() { return device_; }

 protected:
  // Concrete subclasses use this to allocate GpuMemSlabs that are then used
  // to suballocate GpuMem instances from.
  std::unique_ptr<GpuMemSlab> AllocateSlab(vk::DeviceSize size,
                                           uint32_t memory_type_index);
  void FreeSlab(std::unique_ptr<GpuMemSlab> slab);

  // Concrete subclasses use this to sub-allocate GpuMem from GpuMemSlabs.
  GpuMemPtr AllocateMem(GpuMemSlab* slab,
                        vk::DeviceSize offset,
                        vk::DeviceSize size);

 private:
  // Called by GpuMemSlab::FreeMem().
  friend class GpuMemSlab;
  virtual void FreeMem(GpuMemSlab* slab,
                       uint32_t slab_ref_count,
                       vk::DeviceSize offset,
                       vk::DeviceSize size) = 0;

  vk::PhysicalDevice physical_device_;
  vk::Device device_;
  vk::DeviceSize num_bytes_allocated_;

  mutable std::atomic_uint_fast32_t slab_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuAllocator);
};

}  // namespace impl
}  // namespace escher
