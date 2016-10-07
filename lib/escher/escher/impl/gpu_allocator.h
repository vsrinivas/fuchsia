// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

class GpuAllocator;

// Memory allocated by GpuAllocator.
class GpuMem {
 public:
  // Called by GpuAllocator::Allocate().
  GpuMem(vk::DeviceMemory base,
         vk::DeviceSize offset,
         vk::DeviceSize size,
         uint32_t memory_type_index,
         GpuAllocator* allocator);
  GpuMem(GpuMem&& other);
  ~GpuMem();

  vk::DeviceMemory base() const { return base_; }
  vk::DeviceSize offset() const { return offset_; }
  vk::DeviceSize size() const { return size_; }
  uint32_t memory_type_index() const { return memory_type_index_; }

 private:
  vk::DeviceMemory base_;
  vk::DeviceSize offset_;
  vk::DeviceSize size_;
  uint32_t memory_type_index_;
  GpuAllocator* allocator_;
};

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
};

}  // namespace impl
}  // namespace escher
