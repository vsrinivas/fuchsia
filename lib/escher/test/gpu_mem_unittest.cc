// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/vk/gpu_mem.h"
#include "escher/impl/gpu_mem_slab.h"
#include "escher/vk/gpu_allocator.h"
#include "escher/vk/naive_gpu_allocator.h"

#include "gtest/gtest.h"

#include <iostream>

namespace {
using namespace escher;

// Helper function to test sub-allocation of a GpuMem that was allocated in
// different ways.
const vk::DeviceSize kDeviceSize = 1000;
void TestSubAllocation(GpuMemPtr mem) {
  auto sub_alloc1 = mem->Allocate(kDeviceSize, 0);
  auto sub_alloc2 = mem->Allocate(kDeviceSize + 1, 0);
  auto sub_alloc3 = mem->Allocate(kDeviceSize, 1);
  auto sub_alloc4 = mem->Allocate(kDeviceSize, 0);
  // Valid sub-allocation.
  EXPECT_NE(nullptr, sub_alloc1.get());
  // Invalid sub-allocation due to increased size.
  EXPECT_EQ(nullptr, sub_alloc2.get());
  // Invalid sub-allocation due to same size but increased offset.
  EXPECT_EQ(nullptr, sub_alloc3.get());
  // Valid sub-allocation, even though it has 100% overlap with |sub_alloc1|.
  EXPECT_NE(nullptr, sub_alloc4.get());

  // Can sub-allocate from a sub-allocation...
  auto sub_alloc5 = sub_alloc1->Allocate(kDeviceSize / 2, kDeviceSize / 2);
  EXPECT_NE(nullptr, sub_alloc5.get());
  // ... and sub-allocate again from that sub-allocation.  As before, the size
  // and offset of the sub-allocation must fit within the parent.
  auto sub_alloc6 = sub_alloc5->Allocate(kDeviceSize / 2, 0);
  auto sub_alloc7 = sub_alloc5->Allocate(kDeviceSize / 2 + 1, 0);
  auto sub_alloc8 = sub_alloc5->Allocate(kDeviceSize / 2, 1);
  auto sub_alloc9 = sub_alloc5->Allocate(kDeviceSize / 2, 0);
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
  const uint32_t kMemoryTypeIndex = 0;
  auto mem = GpuMem::New(vk::Device(), vk::DeviceMemory(), kDeviceSize,
                         kMemoryTypeIndex);
  TestSubAllocation(mem);
}

TEST(GpuMem, NaiveAllocator) {
  VulkanContext context(vk::Instance(), vk::PhysicalDevice(), vk::Device(),
                        vk::Queue(), 0, vk::Queue(), 0);
  NaiveGpuAllocator allocator(context);

  // Standard sub-allocation tests.
  auto alloc =
      allocator.Allocate({kDeviceSize, 0, 0}, vk::MemoryPropertyFlags());
  EXPECT_EQ(kDeviceSize, allocator.total_slab_bytes());
  TestSubAllocation(alloc);

  // Adding sub-allocations doesn't increase slab-count.
  EXPECT_EQ(1U, allocator.slab_count());
  auto sub_alloc1 = alloc->Allocate(kDeviceSize, 0);
  auto sub_alloc1a = sub_alloc1->Allocate(kDeviceSize, 0);
  auto sub_alloc1b = sub_alloc1->Allocate(kDeviceSize, 0);
  auto sub_alloc2 = alloc->Allocate(kDeviceSize, 0);
  auto sub_alloc2a = sub_alloc2->Allocate(kDeviceSize, 0);
  auto sub_alloc2b = sub_alloc2->Allocate(kDeviceSize, 0);
  EXPECT_EQ(1U, allocator.slab_count());

  // Allocating then freeing increases/decreases the slab-count.
  auto alloc2 =
      allocator.Allocate({kDeviceSize, 0, 0}, vk::MemoryPropertyFlags());
  EXPECT_EQ(2U * kDeviceSize, allocator.total_slab_bytes());
  EXPECT_EQ(2U, allocator.slab_count());
  alloc2 = nullptr;
  EXPECT_EQ(1U, allocator.slab_count());

  // Sub-allocations keep parent allocations alive.
  alloc = nullptr;
  EXPECT_EQ(1U, allocator.slab_count());
  sub_alloc1 = nullptr;
  sub_alloc1a = nullptr;
  sub_alloc1b = nullptr;
  sub_alloc2 = nullptr;
  sub_alloc2a = nullptr;
  EXPECT_EQ(1U, allocator.slab_count());
  EXPECT_EQ(kDeviceSize, allocator.total_slab_bytes());
  sub_alloc2b = nullptr;
  EXPECT_EQ(0U, allocator.slab_count());
  EXPECT_EQ(0U, allocator.total_slab_bytes());
}

// Used to test GpuAllocator sub-allocation callbacks.
class NaiveGpuAllocatorForCallbackTest : public NaiveGpuAllocator {
 public:
  explicit NaiveGpuAllocatorForCallbackTest(GpuMem** last_slab,
                                            vk::DeviceSize* last_size,
                                            vk::DeviceSize* last_offset)
      : NaiveGpuAllocator(VulkanContext()),
        last_slab_(last_slab),
        last_size_(last_size),
        last_offset_(last_offset) {}

  void OnSuballocationDestroyed(GpuMem* slab,
                                vk::DeviceSize size,
                                vk::DeviceSize offset) override {
    *last_slab_ = slab;
    *last_size_ = size;
    *last_offset_ = offset;
  }

 private:
  GpuMem** const last_slab_ = nullptr;
  vk::DeviceSize* const last_size_ = nullptr;
  vk::DeviceSize* const last_offset_ = nullptr;
};

// Note: this test relies on an allocator that returns raw GpuMemSlabs, not
// sub-allocations.
TEST(GpuMem, AllocatorCallbacks) {
  GpuMem* last_slab = nullptr;
  vk::DeviceSize last_size = 0;
  vk::DeviceSize last_offset = 0;
  NaiveGpuAllocatorForCallbackTest allocator(&last_slab, &last_size,
                                             &last_offset);

  auto alloc1 =
      allocator.Allocate({kDeviceSize, 0, 0}, vk::MemoryPropertyFlags());
  auto alloc2 =
      allocator.Allocate({kDeviceSize, 0, 0}, vk::MemoryPropertyFlags());
  EXPECT_EQ(nullptr, last_slab);

  auto sub_alloc1a = alloc1->Allocate(10, 11);
  auto sub_alloc1b = alloc1->Allocate(20, 12);
  auto sub_alloc2a = alloc2->Allocate(30, 13);
  auto sub_alloc2b = alloc2->Allocate(40, 14);
  // Still no sub-allocations destroyed.
  EXPECT_EQ(nullptr, last_slab);

  auto sub_sub_alloc = sub_alloc1a->Allocate(1, 1);
  sub_alloc1a = nullptr;
  // sub_alloc1a is kept alive by sub_sub_alloc.
  EXPECT_EQ(nullptr, last_slab);

  // Trigger the first notification.
  sub_sub_alloc = nullptr;
  EXPECT_EQ(alloc1.get(), last_slab);
  EXPECT_EQ(10U, last_size);
  EXPECT_EQ(11U, last_offset);

  // Finish the rest in different order than initial allocations.
  sub_alloc2b = nullptr;
  EXPECT_EQ(alloc2.get(), last_slab);
  EXPECT_EQ(40U, last_size);
  EXPECT_EQ(14U, last_offset);
  sub_alloc2a = nullptr;
  EXPECT_EQ(alloc2.get(), last_slab);
  EXPECT_EQ(30U, last_size);
  EXPECT_EQ(13U, last_offset);
  sub_alloc1b = nullptr;
  EXPECT_EQ(alloc1.get(), last_slab);
  EXPECT_EQ(20U, last_size);
  EXPECT_EQ(12U, last_offset);
}

}  // namespace
