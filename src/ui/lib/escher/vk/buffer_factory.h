// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_BUFFER_FACTORY_H_
#define SRC_UI_LIB_ESCHER_VK_BUFFER_FACTORY_H_

#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/vk/buffer.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"

namespace escher {

// BufferFactory allows clients to obtain new Buffers with the desired
// properties. Subclasses are free to implement custom caching/recycling
// behaviors. All buffers obtained from a BufferFactory must be released before
// the BufferFactory is destroyed.
class BufferFactory {
 public:
  virtual ~BufferFactory() = default;

  // Creates a buffer, backed by a block of memory. If |out_ptr| is non-null,
  // the buffer will be bound to a dedicated piece of memory (i.e.,
  // VkMemoryDedicatedRequirements.requiresDedicatedAllocation
  // == true). That memory must be accessable through the GpuMem returned in
  // |out_ptr|.
  virtual BufferPtr NewBuffer(vk::DeviceSize size,
                              vk::BufferUsageFlags usage_flags,
                              vk::MemoryPropertyFlags memory_property_flags,
                              GpuMemPtr* out_ptr = nullptr) = 0;
};

// This default implementation allocates memory and creates a new
// Buffer using the provided allocator and manager. The intent is for this class
// to adapt existing GpuAllocators to the BufferFactory interface (i.e.
// equivalent to a partial bind). Classes that wish to implement their own
// caching logic should subclass BufferFactory directly, instead of injecting
// tricky subclasses of GpuAllocator and ResourceManager into this object.
class BufferFactoryAdapter final : public BufferFactory {
 public:
  BufferFactoryAdapter(GpuAllocator* allocator, ResourceManager* manager)
      : allocator_(allocator->GetWeakPtr()), manager_(manager) {}

  BufferPtr NewBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage_flags,
                      vk::MemoryPropertyFlags memory_property_flags,
                      GpuMemPtr* out_ptr = nullptr) final {
    FXL_DCHECK(allocator_);
    return allocator_->AllocateBuffer(manager_, size, usage_flags,
                                      memory_property_flags, out_ptr);
  }

 private:
  const GpuAllocatorWeakPtr allocator_;
  ResourceManager* const manager_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_BUFFER_FACTORY_H_
