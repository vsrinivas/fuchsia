// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_UNIFORM_BUFFER_POOL_H_
#define LIB_ESCHER_IMPL_UNIFORM_BUFFER_POOL_H_

#include <vector>
#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/vk/buffer.h"

namespace escher {
namespace impl {

// Vends 64kB host-accessible Buffers whose resources are automatically returned
// to the pool upon destruction.  If necessary, it will grow by creating new
// buffers (and allocating backing memory for them).  |additional_flags| allows
// the user to customize the memory that is allocated by the pool; by default,
// only eHostVisible is used.  Not thread-safe.
class UniformBufferPool : public ResourceManager {
 public:
  UniformBufferPool(
      EscherWeakPtr escher,
      // If no allocator is provided, Escher's default allocator will be used.
      GpuAllocator* allocator = nullptr,
      vk::MemoryPropertyFlags additional_flags = vk::MemoryPropertyFlags());
  ~UniformBufferPool();

  BufferPtr Allocate();

 private:
  // Implement ResourceManager::OnReceiveOwnable().
  void OnReceiveOwnable(std::unique_ptr<Resource> resource) override;

  // Create a batch of new buffers, which are added to free_buffers_.
  void InternalAllocate();

  // Used to allocate backing memory for the pool's buffers.
  GpuAllocator* const allocator_;

  // Specify the properties of the memory used to back the pool's buffers (e.g.
  // host-visible and coherent).
  const vk::MemoryPropertyFlags flags_;

  // The size of each allocated buffer.
  const vk::DeviceSize buffer_size_;

  // List of free buffers that are available for allocation.
  std::vector<std::unique_ptr<Buffer>> free_buffers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UniformBufferPool);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_UNIFORM_BUFFER_POOL_H_
