// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_NAIVE_GPU_ALLOCATOR_H_
#define LIB_ESCHER_VK_NAIVE_GPU_ALLOCATOR_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/vk/gpu_allocator.h"
#include "lib/escher/vk/gpu_mem.h"
#include "lib/escher/vk/vulkan_context.h"

namespace escher {

// NaiveGpuAllocator uses a separate GpuMemSlab for each GpuMem that it
// allocates.  This ignores Vulkan best practices, and is a placeholder until a
// more sophisticated allocator is written.
class NaiveGpuAllocator : public GpuAllocator {
 public:
  NaiveGpuAllocator(const VulkanContext& context);

  GpuMemPtr Allocate(vk::MemoryRequirements reqs,
                     vk::MemoryPropertyFlags flags) override;

 private:
  // No-op, because NaiveGpuAllocator does not perform sub-allocation.  This
  // can only be called if a client manually sub-allocates from the allocation.
  void OnSuballocationDestroyed(GpuMem* slab, vk::DeviceSize size,
                                vk::DeviceSize offset) override;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_NAIVE_GPU_ALLOCATOR_H_
