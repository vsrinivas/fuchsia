// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/resources/waitable_resource.h"

namespace escher {

class Buffer;
typedef ftl::RefPtr<Buffer> BufferPtr;

// Escher's standard interface to Vulkan buffer objects.
class Buffer : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Construct an ownerless Buffer.  When the Buffer is destroyed, all resources
  // are immediately freed/destroyed.
  Buffer(ResourceManager* manager,
         GpuAllocator* allocator,
         vk::DeviceSize size,
         vk::BufferUsageFlags usage_flags,
         vk::MemoryPropertyFlags memory_property_flags);

  static BufferPtr New(ResourceManager* manager,
                       GpuAllocator* allocator,
                       vk::DeviceSize size,
                       vk::BufferUsageFlags usage_flags,
                       vk::MemoryPropertyFlags memory_property_flags);

  static BufferPtr New(ResourceManager* manager,
                       GpuMemPtr mem,
                       vk::BufferUsageFlags usage_flags,
                       vk::DeviceSize size,
                       vk::DeviceSize offset = 0);

  Buffer(ResourceManager* manager,
         GpuMemPtr mem,
         vk::Buffer buffer,
         vk::DeviceSize size,
         vk::DeviceSize offset = 0);

  ~Buffer() override;

  // Return the underlying Vulkan buffer object.
  vk::Buffer get() { return buffer_; }

  // Return the size of the buffer.
  vk::DeviceSize size() const { return size_; }

  // If the buffer is host-accessible, then this returns a direct pointer to
  // cache-coherent device memory.  Otherwise, returns nullptr.
  uint8_t* ptr() const { return ptr_; }

  const GpuMemPtr& mem() const { return mem_; }

 private:
  GpuMemPtr mem_;
  // Underlying Vulkan buffer object.
  vk::Buffer buffer_;
  // Size of the buffer.
  vk::DeviceSize size_;
  // Pointer to mapped, cache-coherent, host-accessible memory.  Or nullptr.
  uint8_t* ptr_;
};

}  // namespace escher
