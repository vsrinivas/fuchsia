// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_NAIVE_GPU_ALLOCATOR_H_
#define SRC_UI_LIB_ESCHER_VK_NAIVE_GPU_ALLOCATOR_H_

#include "src/ui/lib/escher/impl/gpu_mem_slab.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

#include <vulkan/vulkan.hpp>

namespace escher {

// NaiveGpuAllocator uses a separate GpuMemSlab for each GpuMem that it
// allocates.  This ignores Vulkan best practices, and is a placeholder until a
// more sophisticated allocator is written.
class NaiveGpuAllocator : public GpuAllocator {
 public:
  NaiveGpuAllocator(const VulkanContext& context);
  ~NaiveGpuAllocator();

  // |GpuAllocator|
  GpuMemPtr AllocateMemory(vk::MemoryRequirements reqs, vk::MemoryPropertyFlags flags) override;

  // |GpuAllocator|
  BufferPtr AllocateBuffer(ResourceManager* manager, vk::DeviceSize size,
                           vk::BufferUsageFlags usage_flags,
                           vk::MemoryPropertyFlags memory_property_flags,
                           GpuMemPtr* out_ptr) override;

  // |GpuAllocator|
  ImagePtr AllocateImage(ResourceManager* manager, const escher::ImageInfo& info,
                         GpuMemPtr* out_ptr) override;

  // |GpuAllocator|
  size_t GetTotalBytesAllocated() const override;

  // |GpuAllocator|
  size_t GetUnusedBytesAllocated() const override {
    // Only the minimum needed memory size is allocated, though it's possible
    // the driver may round up the allocation size.
    return 0u;
  }

 private:
  // Callbacks to allow a GpuMemSlab to notify its GpuAllocator of changes.
  friend class impl::GpuMemSlab;
  void OnSlabCreated(vk::DeviceSize slab_size);
  void OnSlabDestroyed(vk::DeviceSize slab_size);

  vk::PhysicalDevice physical_device_;
  vk::Device device_;
  vk::DeviceSize total_slab_bytes_ = 0;
  size_t slab_count_ = 0;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_NAIVE_GPU_ALLOCATOR_H_
