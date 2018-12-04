// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/gpu_mem.h"
#include "gtest/gtest.h"
#include "lib/escher/impl/gpu_mem_slab.h"
#include "lib/escher/test/gtest_vulkan.h"
#include "lib/escher/vk/gpu_allocator.h"
#include "lib/escher/vk/naive_gpu_allocator.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/escher/vk/vulkan_device_queues.h"

#include <iostream>

namespace {
using namespace escher;

// Helper function to test sub-allocation of a GpuMem that was allocated in
// different ways.
const vk::DeviceSize kDeviceSize = 1000;
void TestSubAllocation(GpuMemPtr mem) {
  auto sub_alloc1 = mem->Suballocate(kDeviceSize, 0);
  auto sub_alloc2 = mem->Suballocate(kDeviceSize + 1, 0);
  auto sub_alloc3 = mem->Suballocate(kDeviceSize, 1);
  auto sub_alloc4 = mem->Suballocate(kDeviceSize, 0);
  // Valid sub-allocation.
  EXPECT_NE(nullptr, sub_alloc1.get());
  // Invalid sub-allocation due to increased size.
  EXPECT_EQ(nullptr, sub_alloc2.get());
  // Invalid sub-allocation due to same size but increased offset.
  EXPECT_EQ(nullptr, sub_alloc3.get());
  // Valid sub-allocation, even though it has 100% overlap with |sub_alloc1|.
  EXPECT_NE(nullptr, sub_alloc4.get());

  // Can sub-allocate from a sub-allocation...
  auto sub_alloc5 = sub_alloc1->Suballocate(kDeviceSize / 2, kDeviceSize / 2);
  EXPECT_NE(nullptr, sub_alloc5.get());
  // ... and sub-allocate again from that sub-allocation.  As before, the size
  // and offset of the sub-allocation must fit within the parent.
  auto sub_alloc6 = sub_alloc5->Suballocate(kDeviceSize / 2, 0);
  auto sub_alloc7 = sub_alloc5->Suballocate(kDeviceSize / 2 + 1, 0);
  auto sub_alloc8 = sub_alloc5->Suballocate(kDeviceSize / 2, 1);
  auto sub_alloc9 = sub_alloc5->Suballocate(kDeviceSize / 2, 0);
  // Valid sub-allocation.
  EXPECT_NE(nullptr, sub_alloc6.get());
  // Invalid sub-allocation due to increased size.
  EXPECT_EQ(nullptr, sub_alloc7.get());
  // Invalid sub-allocation due to same size but increased offset.
  EXPECT_EQ(nullptr, sub_alloc8.get());
  // Valid sub-allocation, even though it has 100% overlap with |sub_alloc1|.
  EXPECT_NE(nullptr, sub_alloc9.get());
}

// This test should be updated to include all hashed types used by Escher.
TEST(GpuMem, WrapExisting) {
  auto mem = GpuMem::AdoptVkMemory(vk::Device(), vk::DeviceMemory(),
                                   kDeviceSize, false /* needs_mapped_ptr */);
  TestSubAllocation(mem);
}

VK_TEST(GpuMem, NaiveAllocator) {
  escher::VulkanInstance::Params instance_params(
      {{"VK_LAYER_LUNARG_standard_validation"},
       {VK_EXT_DEBUG_REPORT_EXTENSION_NAME},
       false});

  auto vulkan_instance =
      escher::VulkanInstance::New(std::move(instance_params));
  auto vulkan_device = escher::VulkanDeviceQueues::New(vulkan_instance, {});
  NaiveGpuAllocator allocator(vulkan_device->GetVulkanContext());

  // Standard sub-allocation tests.
  auto alloc = allocator.AllocateMemory(
      {kDeviceSize, 0, 0xffffffff}, vk::MemoryPropertyFlagBits::eDeviceLocal);
  EXPECT_EQ(kDeviceSize, allocator.GetTotalBytesAllocated());
  TestSubAllocation(alloc);

  // Adding sub-allocations doesn't increase slab-count.
  EXPECT_EQ(kDeviceSize, allocator.GetTotalBytesAllocated());
  auto sub_alloc1 = alloc->Suballocate(kDeviceSize, 0);
  auto sub_alloc1a = sub_alloc1->Suballocate(kDeviceSize, 0);
  auto sub_alloc1b = sub_alloc1->Suballocate(kDeviceSize, 0);
  auto sub_alloc2 = alloc->Suballocate(kDeviceSize, 0);
  auto sub_alloc2a = sub_alloc2->Suballocate(kDeviceSize, 0);
  auto sub_alloc2b = sub_alloc2->Suballocate(kDeviceSize, 0);
  EXPECT_EQ(kDeviceSize, allocator.GetTotalBytesAllocated());

  // Allocating then freeing increases/decreases the slab-count.
  auto alloc2 = allocator.AllocateMemory(
      {kDeviceSize, 0, 0xffffffff}, vk::MemoryPropertyFlagBits::eHostVisible);
  EXPECT_EQ(2U * kDeviceSize, allocator.GetTotalBytesAllocated());
  alloc2 = nullptr;
  EXPECT_EQ(kDeviceSize, allocator.GetTotalBytesAllocated());

  // Sub-allocations keep parent allocations alive.
  alloc = nullptr;
  EXPECT_EQ(kDeviceSize, allocator.GetTotalBytesAllocated());
  sub_alloc1 = nullptr;
  sub_alloc1a = nullptr;
  sub_alloc1b = nullptr;
  sub_alloc2 = nullptr;
  sub_alloc2a = nullptr;
  EXPECT_EQ(kDeviceSize, allocator.GetTotalBytesAllocated());
  sub_alloc2b = nullptr;
  EXPECT_EQ(0U, allocator.GetTotalBytesAllocated());
}

namespace {
class FakeGpuMem : public GpuMem {
 public:
  FakeGpuMem(vk::DeviceMemory base, vk::DeviceSize size, vk::DeviceSize offset,
             uint8_t* mapped_ptr)
      : GpuMem(base, size, offset, mapped_ptr) {}
};
}  // anonymous namespace

TEST(GpuMem, RecursiveAllocations) {
  const vk::DeviceMemory kVkMem(reinterpret_cast<VkDeviceMemory>(10000));

  const vk::DeviceSize kSize0 = 100;
  const vk::DeviceSize kOffset0 = 0;
  const vk::DeviceSize kSize1 = 50;
  const vk::DeviceSize kOffset1 = 10;
  const vk::DeviceSize kSize2 = 20;
  const vk::DeviceSize kOffset2 = 20;
  const vk::DeviceSize kSize3 = 5;
  const vk::DeviceSize kOffset3 = 10;

  GpuMemPtr mem =
      fxl::MakeRefCounted<FakeGpuMem>(kVkMem, kSize0, kOffset0, nullptr);
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

TEST(GpuMem, MappedPointer) {
  const vk::DeviceMemory kVkMem(reinterpret_cast<VkDeviceMemory>(10000));

  uint8_t* const kNullPtr = nullptr;
  uint8_t* const kFakePtr = reinterpret_cast<uint8_t*>(1000);
  const vk::DeviceSize kSize1 = 100;
  const vk::DeviceSize kOffset1 = 0;
  const vk::DeviceSize kSize2 = 50;
  const vk::DeviceSize kOffset2 = 10;
  const vk::DeviceSize kSize3 = 20;
  const vk::DeviceSize kOffset3 = 20;

  GpuMemPtr mem = fxl::MakeRefCounted<FakeGpuMem>(vk::DeviceMemory(), kSize1,
                                                  kOffset1, kNullPtr);
  GpuMemPtr sub = mem->Suballocate(kSize2, kOffset2);
  GpuMemPtr subsub = sub->Suballocate(kSize3, kOffset3);
  EXPECT_EQ(nullptr, mem->mapped_ptr());
  EXPECT_EQ(nullptr, sub->mapped_ptr());
  EXPECT_EQ(nullptr, subsub->mapped_ptr());

  mem = fxl::MakeRefCounted<FakeGpuMem>(vk::DeviceMemory(), kSize1, kOffset1,
                                        kFakePtr);
  sub = mem->Suballocate(kSize2, kOffset2);
  subsub = sub->Suballocate(kSize3, kOffset3);
  EXPECT_EQ(kFakePtr, mem->mapped_ptr());
  EXPECT_EQ(static_cast<ptrdiff_t>(kOffset2),
            sub->mapped_ptr() - mem->mapped_ptr());
  EXPECT_EQ(static_cast<ptrdiff_t>(kOffset3 + kOffset2),
            subsub->mapped_ptr() - mem->mapped_ptr());
  EXPECT_EQ(static_cast<ptrdiff_t>(kOffset3),
            subsub->mapped_ptr() - sub->mapped_ptr());
}

}  // namespace
