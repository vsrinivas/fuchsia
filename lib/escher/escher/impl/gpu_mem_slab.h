// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/impl/gpu_mem.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

class GpuAllocator;

class GpuMemSlab;
typedef ftl::RefPtr<GpuMemSlab> GpuMemSlabPtr;

// GpuMemSlab represents a single Vulkan memory allocation, which a GpuAllocator
// may use to sub-allocate multiple GpuMem instances.
class GpuMemSlab : public GpuMem {
 public:
  uint32_t memory_type_index() const { return memory_type_index_; }

  ~GpuMemSlab();

 private:
  friend class GpuAllocator;
  friend class GpuMem;
  static GpuMemSlabPtr New(vk::Device device,
                           vk::PhysicalDevice physical_device,
                           vk::MemoryRequirements reqs,
                           vk::MemoryPropertyFlags flags,
                           GpuAllocator* allocator);
  GpuMemSlab(vk::Device device,
             vk::DeviceMemory base,
             vk::DeviceSize size,
             uint32_t memory_type_index,
             GpuAllocator* allocator = nullptr);

  vk::Device device_;
  uint32_t memory_type_index_;
  GpuAllocator* allocator_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuMemSlab);
};

}  // namespace impl
}  // namespace escher
