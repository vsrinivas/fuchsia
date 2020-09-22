// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/naive_buffer.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"

namespace escher {

using BufferTest = escher::test::TestWithVkValidationLayer;

VK_TEST_F(BufferTest, CreateWithPreExistingMemory) {
  auto escher = test::GetEscher();
  auto allocator = escher->gpu_allocator();
  auto recycler = escher->resource_recycler();

  constexpr vk::DeviceSize kDummyBufferSize = 10000;
  // TODO(fxbug.dev/24563): Scenic may use a different set of bits when creating a
  // buffer, resulting in a memory pool mismatch.
  const auto kBufferUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
  const auto kMemoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

  // This is silly, but without creating a buffer, I don't understand how to
  // populate vk::MemoryRequirements::memoryTypeBits.
  auto dummy_buffer = allocator->AllocateBuffer(recycler, kDummyBufferSize, kBufferUsageFlags,
                                                kMemoryPropertyFlags);
  vk::MemoryRequirements reqs = escher->vk_device().getBufferMemoryRequirements(dummy_buffer->vk());

  // Now that we have the memory requirements, we can allocate some memory, so
  // that we can test creating a buffer with pre-existing memory.
  auto mem1 = allocator->AllocateMemory(reqs, kMemoryPropertyFlags);

  // Suballocate some memory. But before sub-allocation, we need to get the
  // memory requirements of the "smaller" buffer as well.
  constexpr vk::DeviceSize kBufferSize = 1000;
  constexpr vk::DeviceSize kOffset = 512;
  auto dummy_buffer_2 =
      allocator->AllocateBuffer(recycler, kBufferSize, kBufferUsageFlags, kMemoryPropertyFlags);
  vk::MemoryRequirements reqs_2 =
      escher->vk_device().getBufferMemoryRequirements(dummy_buffer_2->vk());
  vk::DeviceSize sub_alloc_size = reqs_2.size;
  auto mem2 = mem1->Suballocate(sub_alloc_size, kOffset);
  EXPECT_EQ(mem1->mapped_ptr() + kOffset, mem2->mapped_ptr());

  // Allocate 2 buffers, one from the original allocation, and one from the
  // sub-allocation.
  auto buf1 = impl::NaiveBuffer::New(recycler, mem1, kBufferUsageFlags);
  auto buf2 = impl::NaiveBuffer::New(recycler, mem2, kBufferUsageFlags);
  ASSERT_TRUE(buf1);
  EXPECT_EQ(mem1->mapped_ptr(), buf1->host_ptr());
  ASSERT_TRUE(buf2);
  EXPECT_EQ(mem2->mapped_ptr(), buf2->host_ptr());
}

}  // namespace escher
