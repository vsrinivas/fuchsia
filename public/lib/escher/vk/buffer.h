// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_BUFFER_H_
#define LIB_ESCHER_VK_BUFFER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/waitable_resource.h"
#include "lib/escher/vk/gpu_mem.h"

namespace escher {

class Buffer;
typedef fxl::RefPtr<Buffer> BufferPtr;

// Escher's standard interface to Vulkan buffer objects.
class Buffer : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  static BufferPtr New(ResourceManager* manager, GpuAllocator* allocator,
                       vk::DeviceSize size, vk::BufferUsageFlags usage_flags,
                       vk::MemoryPropertyFlags memory_property_flags,
                       GpuMemPtr* out_ptr = nullptr);

  static BufferPtr New(ResourceManager* manager, GpuMemPtr mem,
                       vk::BufferUsageFlags usage_flags);

  Buffer(ResourceManager* manager, GpuMemPtr mem, vk::Buffer buffer);

  ~Buffer() override;

  // Return the underlying Vulkan buffer object.
  vk::Buffer vk() { return buffer_; }

  // Return the size of the buffer.
  vk::DeviceSize size() const { return mem_->size(); }

  // If the buffer is host-accessible, then this returns a direct pointer to
  // cache-coherent device memory.  Otherwise, returns nullptr.
  uint8_t* host_ptr() const { return mem_->mapped_ptr(); }

 private:
  // Backing memory object.
  GpuMemPtr mem_;
  // Underlying Vulkan buffer object.
  vk::Buffer buffer_;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_BUFFER_H_
