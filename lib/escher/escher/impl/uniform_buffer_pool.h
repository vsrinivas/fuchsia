// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>

#include <escher/forward_declarations.h>
#include <escher/impl/resource.h>

namespace escher {
namespace impl {

class GpuAllocator;
class UniformBufferPool;

// A 64kB host-accessible uniform buffer that returns its resources to its
// UniformBufferPool upon destruction.  Not thread-safe.
class UniformBuffer : public Resource {
 public:
  ~UniformBuffer() override;

  vk::Buffer get() { return buffer_; }
  vk::DeviceSize size() const;
  UniformBufferPool* pool() { return pool_; }
  uint8_t* ptr() const { return ptr_; }

 private:
  friend class UniformBufferPool;
  UniformBuffer(UniformBufferPool* pool,
                vk::Buffer buffer,
                uint8_t* mapped_ptr);

  UniformBufferPool* pool_;
  vk::Buffer buffer_;
  uint8_t* ptr_;
};

typedef ftl::RefPtr<UniformBuffer> UniformBufferPtr;

// Vends UniformBuffers whose resources are automatically returned to the pool
// upon destruction.  If necessary, it will grow by creating new buffers (and
// allocating backing memory for them).  Not thread-safe.
class UniformBufferPool {
 public:
  UniformBufferPool(vk::Device device, GpuAllocator* allocator);
  ~UniformBufferPool();

  UniformBufferPtr Allocate();

  vk::DeviceSize buffer_size() const { return buffer_size_; }

 private:
  // Called by ~UniformBuffer to return its buffer to free_buffers_.
  friend class UniformBuffer;
  void ReturnUniformBuffer(vk::Buffer buffer, uint8_t* mapped_ptr);

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

  // List of free buffers that are available for allocation.
  struct MappedBuffer {
    vk::Buffer buffer;
    uint8_t* ptr;
  };
  std::vector<MappedBuffer> free_buffers_;

  // Memory allocated to back all of the buffers created by this pool.
  std::vector<GpuMemPtr> backing_memory_;

  // Number of currently-existing UniformBuffer objects that were created by
  // this pool.
  uint32_t allocation_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UniformBufferPool);
};

// Inline function definitions.

inline vk::DeviceSize UniformBuffer::size() const {
  return pool_->buffer_size();
}

}  // namespace impl
}  // namespace escher
