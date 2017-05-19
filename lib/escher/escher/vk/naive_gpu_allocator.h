// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/vk/gpu_allocator.h"
#include "escher/vk/gpu_mem.h"
#include "escher/vk/vulkan_context.h"

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
  void OnSuballocationDestroyed(GpuMem* slab,
                                vk::DeviceSize size,
                                vk::DeviceSize offset) override;
};

}  // namespace escher
