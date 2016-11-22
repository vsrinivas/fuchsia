// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include <escher/impl/resource.h>

namespace escher {

// Escher's standard interface to Vulkan buffer objects.  Defined below.
class Buffer;

// Interface that allows a Buffer to obtain access to its underlying vk::Buffer,
// its size, etc.  For a given instance of BufferInfo, all methods must always
// return the same value.
class BufferInfo {
 public:
  virtual ~BufferInfo() {}

  virtual vk::Buffer GetBuffer() = 0;
  virtual vk::DeviceSize GetSize() = 0;
  virtual uint8_t* GetMappedPointer() = 0;
};

// Interface that allows BufferInfo to be recycled when a Buffer is
// destroyed.
class BufferOwner {
 public:
  virtual void RecycleBuffer(std::unique_ptr<BufferInfo> info) = 0;

 protected:
  // Allows subclasses to instantiate buffers.  Owner must be guaranteed to
  // outlive the returned Buffer.
  BufferPtr NewBuffer(std::unique_ptr<BufferInfo> info);
};

// Escher's standard interface to Vulkan buffer objects.
class Buffer : public impl::Resource {
 public:
  // Construct an ownerless Buffer.  When the Buffer is destroyed, all resources
  // are immediately freed/destroyed.
  Buffer(vk::Device device,
         impl::GpuAllocator* allocator,
         vk::DeviceSize size,
         vk::BufferUsageFlags usage_flags,
         vk::MemoryPropertyFlags memory_property_flags);
  ~Buffer() override;

  // Return the underlying Vulkan buffer object.
  vk::Buffer get() { return buffer_; }

  // Return the size of the buffer.
  vk::DeviceSize size() const { return size_; }

  // If the buffer is host-accessible, then this returns a direct pointer to
  // cache-coherent device memory.  Otherwise, returns nullptr.
  uint8_t* ptr() const { return ptr_; }

 private:
  // Called by BufferOwner::NewBuffer().
  friend class BufferOwner;
  Buffer(std::unique_ptr<BufferInfo> info, BufferOwner* owner);

  // When the Buffer is destroyed, these resources will either be recycled or
  // immediately destroyed, depending on whether owner_ is nullptr.
  std::unique_ptr<BufferInfo> info_;
  // Underlying Vulkan buffer object.
  vk::Buffer buffer_;
  // Size of the buffer.
  vk::DeviceSize size_;
  // Pointer to mapped, cache-coherent, host-accessible memory.  Or nullptr.
  uint8_t* ptr_;
  // Notified when Buffer is destroyed.
  BufferOwner* owner_;
};

}  // namespace escher
