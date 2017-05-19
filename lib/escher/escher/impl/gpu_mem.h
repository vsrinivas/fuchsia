// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "ftl/macros.h"
#include "ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

class GpuMem;
typedef ftl::RefPtr<GpuMem> GpuMemPtr;

// Ref-counted wrapper around a vk::DeviceMemory.  Supports sub-allocation.
// TODO: move out of impl namespace.
class GpuMem : public ftl::RefCountedThreadSafe<GpuMem> {
 public:
  // Create a GpuMem that wraps a newly-allocated vk::DeviceMemory, which will
  // be destroyed when the GpuMem dies.
  static GpuMemPtr New(vk::Device device,
                       vk::PhysicalDevice physical_device,
                       vk::MemoryRequirements reqs,
                       vk::MemoryPropertyFlags flags);

  // Create a GpuMem that takes ownership of |mem|, which will be destroyed hen
  // the GpuMem dies.
  static GpuMemPtr New(vk::Device device,
                       vk::DeviceMemory mem,
                       vk::DeviceSize size,
                       uint32_t memory_type_index);

  // Sub-allocate a GpuMem that represents a sub-range of the memory in this
  // GpuMem.  Since these sub-allocations reference the parent GpuMem, the
  // parent will not be destroyed while outstanding sub-allocations exist.
  // Returns nullptr if the requested offset/size do not fit within the current
  // GpuMem.
  // Note: no bookkeeping ensures that sub-allocations do not overlap!
  GpuMemPtr Allocate(vk::DeviceSize size, vk::DeviceSize offset);

  vk::DeviceMemory base() const { return base_; }
  vk::DeviceSize offset() const { return offset_; }
  vk::DeviceSize size() const { return size_; }

 protected:
  // |offset| + |size| must be <= the size of |base|.  Takes ownership of
  // |base|.
  GpuMem(vk::DeviceMemory base, vk::DeviceSize size, vk::DeviceSize offset = 0);

  FRIEND_REF_COUNTED_THREAD_SAFE(GpuMem);
  virtual ~GpuMem();

 private:
  // Allow subclasses to take action when a sub-allocation is destroyed.  For
  // example, this can be used by subclasses of GpuAllocator for bookkeeping of
  // available memory withing a GpuMemSlab.
  friend class GpuMemSuballocation;
  virtual void OnAllocationDestroyed(vk::DeviceSize size,
                                     vk::DeviceSize offset) {}

  vk::DeviceMemory base_;
  vk::DeviceSize size_;
  vk::DeviceSize offset_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuMem);
};

}  // namespace impl
}  // namespace escher
