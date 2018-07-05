// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_GPU_ALLOCATOR_H_
#define LIB_ESCHER_VK_GPU_ALLOCATOR_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/impl/gpu_mem_slab.h"
#include "lib/escher/vk/vulkan_context.h"

namespace escher {

// GpuAllocator provides a framework for malloc()-like sub-allocation of Vulkan
// memory.  Vulkan does not require implementations to support large numbers of
// memory allocations.  Instead, applications are expected to allocate larger
// chunks of memory, and sub-allocate from within these chunks.
//
// GpuAllocator defines the interface that clients use to obtain sub-allocated
// memory; the specific sub-allocation strategy employed is the responsibility
// of concrete subclasses of GpuAllocator.
//
// Implementation notes:
// The following is only of interest to implementors of GpuAllocator subclasses.
//
// Unbeknownst to clients, GpuMem::Allocate() and GpuAllocator::Allocate() do
// not return an instance of GpuMem, but rather an instance of a concrete
// subclass.  There are two concrete subclasses of GpuMem (with no plans to
// create more):
//   - impl::GpuMemSuballocation
//   - impl::GpuMemSlab
//
// GpuMemSuballocation represents a subset of a parent GpuMem; the object
// returned from GpuMem::Allocate() is always a GpuMemSuballocation.  This is
// also the type returned by GpuAllocator::Allocate() when a subclass performs
// a sub-allocation of an existing GpuMem (in fact, the subclass implementation
// uses GpuMem::Allocate() to perform this sub-allocation).
//
// However, sometimes a GpuAllocator has insufficient free space to sub-allocate
// from, and must allocate more memory directly from Vulkan.  This is done by
// calling the protected method GpuAllocator::AllocateSlab().  The returned
// GpuMem object is actually a GpuMemSlab, but the subclass neither knows nor
// cares (except indirectly: GpuMemSlab overrides OnAllocationDestroyed() to
// call GpuAllocator::OnSuballocationDestroyed()).
class GpuAllocator {
 public:
  GpuAllocator(const VulkanContext& context);
  virtual ~GpuAllocator();

  virtual GpuMemPtr Allocate(vk::MemoryRequirements reqs,
                             vk::MemoryPropertyFlags flags) = 0;

  vk::PhysicalDevice physical_device() const { return physical_device_; }
  vk::Device device() const { return device_; }

  // Current number of bytes allocated (i.e. unfreed) by this allocator.
  // This is the sum of all slabs allocated by AllocateSlab(), even if the
  // no sub-allocations have been made from these slabs.
  uint64_t total_slab_bytes() { return total_slab_bytes_; }
  uint32_t slab_count() const { return slab_count_; }

 protected:
  // Concrete subclasses use this to allocate a slab of memory directly from
  // Vulkan.  Sub-allocation can then be performed via GpuMem::Allocate().
  GpuMemPtr AllocateSlab(vk::MemoryRequirements reqs,
                         vk::MemoryPropertyFlags flags);

 private:
  // Callbacks to allow a GpuMemSlab to notify its GpuAllocator of changes.
  friend class impl::GpuMemSlab;
  void OnSlabCreated(vk::DeviceSize slab_size);
  void OnSlabDestroyed(vk::DeviceSize slab_size);
  // Notify the GpuAllocator that a sub-allocated range of memory is no longer
  // used within the specified slab.
  virtual void OnSuballocationDestroyed(GpuMem* slab, vk::DeviceSize size,
                                        vk::DeviceSize offset) = 0;

  vk::PhysicalDevice physical_device_;
  vk::Device device_;
  vk::DeviceSize total_slab_bytes_ = 0;
  size_t slab_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(GpuAllocator);
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_GPU_ALLOCATOR_H_
