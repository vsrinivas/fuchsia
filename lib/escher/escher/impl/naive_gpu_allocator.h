// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/impl/gpu_allocator.h"
#include "escher/impl/gpu_mem.h"
#include "escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

// NaiveGpuAllocator uses a separate GpuMemSlab for each GpuMem that it
// allocates.  This ignores Vulkan best practices, and is a placeholder until a
// more sophisticated allocator is written.
class NaiveGpuAllocator : public GpuAllocator {
 public:
  NaiveGpuAllocator(const VulkanContext& context);

  GpuMemPtr Allocate(vk::MemoryRequirements reqs,
                     vk::MemoryPropertyFlags flags) override;

 private:
  void FreeMem(GpuMemSlab* slab,
               uint32_t slab_ref_count,
               vk::DeviceSize offset,
               vk::DeviceSize size) override;
};

}  // namespace impl
}  // namespace escher
