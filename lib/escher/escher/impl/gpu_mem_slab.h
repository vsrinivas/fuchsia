// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "ftl/macros.h"

namespace escher {
namespace impl {

class GpuAllocator;
class GpuMem;

// GpuMemSlab represents a single Vulkan memory allocation, which a GpuAllocator
// may use to sub-allocate multiple GpuMem instances.
class GpuMemSlab {
 public:
  vk::DeviceMemory base() const { return base_; }
  vk::DeviceSize size() const { return size_; }
  uint32_t memory_type_index() const { return memory_type_index_; }

  ~GpuMemSlab();

 private:
  friend class GpuAllocator;
  GpuMemSlab(vk::DeviceMemory base,
             vk::DeviceSize size,
             uint32_t memory_type_index,
             GpuAllocator* allocator);

  // Slab's ref-count is adjusted by GpuMem's constructor and destructor.
  friend class GpuMem;
  void AddRef();
  void FreeMem(vk::DeviceSize offset, vk::DeviceSize size);

  vk::DeviceMemory base_;
  vk::DeviceSize size_;
  uint32_t memory_type_index_;
  GpuAllocator* allocator_;
  mutable std::atomic_uint_fast32_t ref_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuMemSlab);
};

}  // namespace impl
}  // namespace escher
