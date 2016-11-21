// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/impl/gpu_mem_slab.h"
#include "ftl/macros.h"
#include "ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

// Memory allocated by a GpuAllocator.  It is a region of a GpuMemSlab (this is
// an implementation detail, not relevant to clients of GpuAllocator/GpuMem).
// TODO: move out of impl namespace.
class GpuMem : public ftl::RefCountedThreadSafe<GpuMem> {
 public:
  // Accessors for the base address of the underlying GpuMemSlab, and the offset
  // and size of the region within the slab.  These support idiomatic use of
  // Vulkan APIs for e.g. creating vk::Images and vk::Buffers.
  vk::DeviceMemory base() const { return slab_->base(); }
  vk::DeviceSize offset() const { return offset_; }
  vk::DeviceSize size() const { return size_; }

  // Return the memory type of the underlying slab.
  uint32_t memory_type_index() const { return slab_->memory_type_index(); }

 private:
  // Called by GpuAllocator::Allocate().
  friend class GpuAllocator;
  GpuMem(GpuMemSlab* slab, vk::DeviceSize offset, vk::DeviceSize size);

  FRIEND_REF_COUNTED_THREAD_SAFE(GpuMem);
  ~GpuMem();

  GpuMemSlab* slab_;
  vk::DeviceSize offset_;
  vk::DeviceSize size_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuMem);
};

typedef ftl::RefPtr<GpuMem> GpuMemPtr;

}  // namespace impl
}  // namespace escher
