// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_VK_FAKE_GPU_ALLOCATOR_H_
#define SRC_UI_LIB_ESCHER_TEST_VK_FAKE_GPU_ALLOCATOR_H_

#include "src/ui/lib/escher/vk/gpu_allocator.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

// This fake gpu allocator does not have a dependency on a functional
// vk::Device. It will make objects with mapped memory, but without working
// Vulkan objects. This should be sufficient for tests that push bits in buffers
// or manage object lifetimes, but will not work for tests that actually want to
// execute Vulkan commands.
//
// All three factory methods can be called with a null ResourceManager.
class FakeGpuAllocator : public GpuAllocator {
 public:
  FakeGpuAllocator();
  ~FakeGpuAllocator();

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
  size_t GetUnusedBytesAllocated() const override { return 0u; }

  // These functions are public because this is a test class, and unit tests may
  // wish to indirectly mock GetTotalBytesAllocated() behavior.
  void OnAllocation(uint64_t size);
  void OnDeallocation(uint64_t size);

 private:
  uint64_t bytes_allocated_ = 0;
};

}  // namespace test
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_VK_FAKE_GPU_ALLOCATOR_H_
