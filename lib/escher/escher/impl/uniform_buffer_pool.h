// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/vk/buffer.h"

namespace escher {
namespace impl {

// Vends 64kB host-accessible Buffers whose resources are automatically returned
// to the pool upon destruction.  If necessary, it will grow by creating new
// buffers (and allocating backing memory for them).  Not thread-safe.
class UniformBufferPool : public BufferOwner {
 public:
  UniformBufferPool(vk::Device device, GpuAllocator* allocator);
  ~UniformBufferPool();

  BufferPtr Allocate();

 private:
  // Implement BufferOwner::RecycleBuffer().
  void RecycleBuffer(std::unique_ptr<BufferInfo> info) override;

  // Create a batch of new buffers, which are added to free_buffers_.
  void InternalAllocate();

  vk::Device device_;

  // Used to allocate backing memory for the pool's buffers.
  GpuAllocator* allocator_;

  // Specify the properties of the memory used to back the pool's buffers (e.g.
  // host-visisble and coherent).
  vk::MemoryPropertyFlags flags_;

  // The size of each allocated buffer.
  vk::DeviceSize buffer_size_;

  // Item in free_buffers_.
  class UniformBufferInfo : public BufferInfo {
   public:
    UniformBufferInfo(vk::Buffer buffer, uint8_t* ptr);
    ~UniformBufferInfo();

    vk::Buffer GetBuffer() override;
    vk::DeviceSize GetSize() override;
    uint8_t* GetMappedPointer() override;

    vk::Buffer buffer;
    uint8_t* ptr;
  };

  // List of free buffers that are available for allocation.
  std::vector<std::unique_ptr<BufferInfo>> free_buffers_;

  // Memory allocated to back all of the buffers created by this pool.
  std::vector<GpuMemPtr> backing_memory_;

  // Number of currently-existing UniformBuffer objects that were created by
  // this pool.
  uint32_t allocation_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UniformBufferPool);
};

}  // namespace impl
}  // namespace escher
