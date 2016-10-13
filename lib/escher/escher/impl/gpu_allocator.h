// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/vk/vulkan_context.h"
#include "escher/impl/gpu_mem.h"

namespace escher {
namespace impl {

// Vulkan does not support large numbers of memory allocations.  Instead,
// applications are expected to allocate larger chunks of memory, and do their
// own memory management within these chunks.
//
// TODO: implement memory management as described above; the current
// implementation uses one Vulkan allocation per call to Allocate().
class GpuAllocator {
 public:
  GpuAllocator(const VulkanContext& context);
  ~GpuAllocator();

  GpuMem Allocate(vk::MemoryRequirements reqs, vk::MemoryPropertyFlags flags);

  uint64_t GetNumBytesAllocated() { return num_bytes_allocated_; }

 private:
  // Called by ~GpuMem().
  friend class GpuMem;
  void Free(GpuMem mem);

  vk::PhysicalDevice physical_device_;
  vk::Device device_;
  vk::DeviceSize num_bytes_allocated_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuAllocator);
};

}  // namespace impl
}  // namespace escher
