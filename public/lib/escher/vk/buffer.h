// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_BUFFER_H_
#define LIB_ESCHER_VK_BUFFER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/waitable_resource.h"

namespace escher {

class Buffer;
typedef fxl::RefPtr<Buffer> BufferPtr;

// Range within the buffer.
struct BufferRange {
  vk::DeviceSize offset;
  vk::DeviceSize size;

  BufferRange(vk::DeviceSize _offset, vk::DeviceSize _size)
      : offset(_offset), size(_size) {}
};

// Escher's standard interface to Vulkan buffer objects.
class Buffer : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  static BufferPtr New(ResourceManager* manager, GpuAllocator* allocator,
                       vk::DeviceSize size, vk::BufferUsageFlags usage_flags,
                       vk::MemoryPropertyFlags memory_property_flags,
                       vk::DeviceSize offset = 0);

  static BufferPtr New(ResourceManager* manager, GpuMemPtr mem,
                       vk::BufferUsageFlags usage_flags, vk::DeviceSize size,
                       vk::DeviceSize offset = 0);

  Buffer(ResourceManager* manager, GpuMemPtr mem, vk::Buffer buffer,
         BufferRange range);

  ~Buffer() override;

  // Return the underlying Vulkan buffer object.
  vk::Buffer vk() { return buffer_; }

  // Return the size of the buffer.
  vk::DeviceSize size() const { return range_.size; }

  // If the buffer is host-accessible, then this returns a direct pointer to
  // cache-coherent device memory.  Otherwise, returns nullptr.
  uint8_t* host_ptr() const { return host_ptr_; }

  const GpuMemPtr& mem() const { return mem_; }

 private:
  GpuMemPtr mem_;
  // Underlying Vulkan buffer object.
  vk::Buffer buffer_;
  // Size of the buffer.
  BufferRange range_;
  // Pointer to mapped, cache-coherent, host-accessible memory.  Or nullptr.
  uint8_t* host_ptr_;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_BUFFER_H_
