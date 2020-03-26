// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/gpu_mem.h"

#include <iostream>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/impl/gpu_mem_slab.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"

namespace {
using namespace escher;

const vk::DeviceSize kTestMemorySize = 1000;

class FakeGpuMem : public GpuMem {
 public:
  FakeGpuMem(vk::DeviceMemory base, vk::DeviceSize size, vk::DeviceSize offset, uint8_t* mapped_ptr,
             int* obj_count = nullptr)
      : GpuMem(base, size, offset, mapped_ptr), obj_count_(obj_count) {
    if (obj_count_)
      ++(*obj_count_);
  }

  ~FakeGpuMem() {
    if (obj_count_)
      --(*obj_count_);
  }

 private:
  int* obj_count_;
};

TEST(GpuMem, ErroneousSuballocations) {
  int obj_count = 0;
  GpuMemPtr mem =
      fxl::AdoptRef(new FakeGpuMem(vk::DeviceMemory(), kTestMemorySize, 0u, nullptr, &obj_count));
  EXPECT_EQ(1, obj_count);

  auto sub_alloc1 = mem->Suballocate(kTestMemorySize, 0);
  auto sub_alloc2 = mem->Suballocate(kTestMemorySize + 1, 0);
  auto sub_alloc3 = mem->Suballocate(kTestMemorySize, 1);
  auto sub_alloc4 = mem->Suballocate(kTestMemorySize, 0);

  // Creating sub-allocations does not create more "real" memory objects.
  EXPECT_EQ(1, obj_count);

  // Valid sub-allocation.
  EXPECT_NE(nullptr, sub_alloc1.get());
  // Invalid sub-allocation due to increased size.
  EXPECT_EQ(nullptr, sub_alloc2.get());
  // Invalid sub-allocation due to same size but increased offset.
  EXPECT_EQ(nullptr, sub_alloc3.get());
  // Valid sub-allocation, even though it has 100% overlap with |sub_alloc1|.
  EXPECT_NE(nullptr, sub_alloc4.get());

  // Can sub-allocate from a sub-allocation...
  auto sub_alloc5 = sub_alloc1->Suballocate(kTestMemorySize / 2, kTestMemorySize / 2);
  EXPECT_NE(nullptr, sub_alloc5.get());
  // ... and sub-allocate again from that sub-allocation.  As before, the size
  // and offset of the sub-allocation must fit within the parent.
  auto sub_alloc6 = sub_alloc5->Suballocate(kTestMemorySize / 2, 0);
  auto sub_alloc7 = sub_alloc5->Suballocate(kTestMemorySize / 2 + 1, 0);
  auto sub_alloc8 = sub_alloc5->Suballocate(kTestMemorySize / 2, 1);
  auto sub_alloc9 = sub_alloc5->Suballocate(kTestMemorySize / 2, 0);
  // Valid sub-allocation.
  EXPECT_NE(nullptr, sub_alloc6.get());
  // Invalid sub-allocation due to increased size.
  EXPECT_EQ(nullptr, sub_alloc7.get());
  // Invalid sub-allocation due to same size but increased offset.
  EXPECT_EQ(nullptr, sub_alloc8.get());
  // Valid sub-allocation, even though it has 100% overlap with |sub_alloc1|.
  EXPECT_NE(nullptr, sub_alloc9.get());

  EXPECT_EQ(1, obj_count);
  mem = nullptr;
  // Suballocations keep the base allocation alive.
  EXPECT_EQ(1, obj_count);

  sub_alloc1 = nullptr;
  sub_alloc4 = nullptr;
  sub_alloc5 = nullptr;
  sub_alloc6 = nullptr;
  sub_alloc9 = nullptr;

  // Removing all valid sub-allocations causes the base allocation to go out of
  // scope.
  EXPECT_EQ(0, obj_count);
}

using GpuMemTest = escher::test::TestWithVkValidationLayer;

// This test should be updated to include all hashed types used by Escher.
VK_TEST_F(GpuMemTest, AdoptVkMemory) {
  auto vulkan_instance =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  auto vulkan_queues =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanDevice();
  auto device = vulkan_queues->GetVulkanContext().device;
  auto physical_device = vulkan_queues->GetVulkanContext().physical_device;

  vk::MemoryAllocateInfo info;
  info.allocationSize = kTestMemorySize;
  info.memoryTypeIndex = impl::GetMemoryTypeIndex(physical_device, INT32_MAX,
                                                  vk::MemoryPropertyFlagBits::eHostVisible);
  vk::DeviceMemory vk_mem = ESCHER_CHECKED_VK_RESULT(device.allocateMemory(info));

  // This test only checks for valid creation and destruction. It would need
  // a mock Vulkan to test for memory usage.
  auto mem = GpuMem::AdoptVkMemory(device, vk_mem, kTestMemorySize, true /* needs_mapped_ptr */);
  EXPECT_EQ(vk_mem, mem->base());
  EXPECT_EQ(kTestMemorySize, mem->size());
  EXPECT_EQ(0u, mem->offset());
  EXPECT_NE(nullptr, mem->mapped_ptr());
}

TEST_F(GpuMemTest, RecursiveAllocations) {
  const vk::DeviceMemory kVkMem(reinterpret_cast<VkDeviceMemory>(10000));

  const vk::DeviceSize kSize0 = 100;
  const vk::DeviceSize kOffset0 = 0;
  const vk::DeviceSize kSize1 = 50;
  const vk::DeviceSize kOffset1 = 10;
  const vk::DeviceSize kSize2 = 20;
  const vk::DeviceSize kOffset2 = 20;
  const vk::DeviceSize kSize3 = 5;
  const vk::DeviceSize kOffset3 = 10;

  GpuMemPtr mem = fxl::MakeRefCounted<FakeGpuMem>(kVkMem, kSize0, kOffset0, nullptr);
  auto sub = mem->Suballocate(kSize1, kOffset1);
  auto subsub = sub->Suballocate(kSize2, kOffset2);
  auto subsubsub = subsub->Suballocate(kSize3, kOffset3);

  EXPECT_NE(vk::DeviceMemory(), mem->base());
  EXPECT_EQ(mem->base(), sub->base());
  EXPECT_EQ(sub->base(), subsub->base());
  EXPECT_EQ(subsub->base(), subsubsub->base());

  EXPECT_EQ(kOffset1, sub->offset());
  EXPECT_EQ(kOffset1 + kOffset2, subsub->offset());
  EXPECT_EQ(kOffset1 + kOffset2 + kOffset3, subsubsub->offset());
}

TEST_F(GpuMemTest, MappedPointer) {
  const vk::DeviceMemory kVkMem(reinterpret_cast<VkDeviceMemory>(10000));

  uint8_t* const kNullPtr = nullptr;
  uint8_t* const kFakePtr = reinterpret_cast<uint8_t*>(1000);
  const vk::DeviceSize kSize1 = 100;
  const vk::DeviceSize kOffset1 = 0;
  const vk::DeviceSize kSize2 = 50;
  const vk::DeviceSize kOffset2 = 10;
  const vk::DeviceSize kSize3 = 20;
  const vk::DeviceSize kOffset3 = 20;

  GpuMemPtr mem = fxl::MakeRefCounted<FakeGpuMem>(vk::DeviceMemory(), kSize1, kOffset1, kNullPtr);
  GpuMemPtr sub = mem->Suballocate(kSize2, kOffset2);
  GpuMemPtr subsub = sub->Suballocate(kSize3, kOffset3);
  EXPECT_EQ(nullptr, mem->mapped_ptr());
  EXPECT_EQ(nullptr, sub->mapped_ptr());
  EXPECT_EQ(nullptr, subsub->mapped_ptr());

  mem = fxl::MakeRefCounted<FakeGpuMem>(vk::DeviceMemory(), kSize1, kOffset1, kFakePtr);
  sub = mem->Suballocate(kSize2, kOffset2);
  subsub = sub->Suballocate(kSize3, kOffset3);
  EXPECT_EQ(kFakePtr, mem->mapped_ptr());
  EXPECT_EQ(static_cast<ptrdiff_t>(kOffset2), sub->mapped_ptr() - mem->mapped_ptr());
  EXPECT_EQ(static_cast<ptrdiff_t>(kOffset3 + kOffset2), subsub->mapped_ptr() - mem->mapped_ptr());
  EXPECT_EQ(static_cast<ptrdiff_t>(kOffset3), subsub->mapped_ptr() - sub->mapped_ptr());
}

}  // namespace
