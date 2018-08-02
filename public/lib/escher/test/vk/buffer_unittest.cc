// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/test/gtest_escher.h"

#include "garnet/public/lib/escher/resources/resource_recycler.h"
#include "garnet/public/lib/escher/vk/buffer.h"
#include "garnet/public/lib/escher/vk/gpu_allocator.h"

namespace escher {

VK_TEST(BufferTest, CreateWithPreExistingMemory) {
  auto escher = test::GetEscher();
  auto allocator = escher->gpu_allocator();
  auto recycler = escher->resource_recycler();

  constexpr vk::DeviceSize kDummyBufferSize = 10000;
  const auto kBufferUsageFlags = vk::BufferUsageFlagBits::eTransferSrc |
                                 vk::BufferUsageFlagBits::eTransferDst;
  const auto kMemoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

  // This is silly, but without creating a buffer, I don't understand how to
  // populate vk::MemoryRequirements::memoryTypeBits.
  auto dummy_buffer = Buffer::New(recycler, allocator, kDummyBufferSize,
                                  kBufferUsageFlags, kMemoryPropertyFlags);
  vk::MemoryRequirements reqs =
      escher->vk_device().getBufferMemoryRequirements(dummy_buffer->vk());

  // Now that we have the memory requirements, we can allocate some memory, so
  // that we can test creating a buffer with pre-existing memory.
  auto mem1 = allocator->Allocate(reqs, kMemoryPropertyFlags);

  // Suballocate some memory.
  constexpr vk::DeviceSize kBufferSize = 1000;
  constexpr vk::DeviceSize kOffset = 512;
  auto mem2 = mem1->Allocate(kBufferSize, kOffset);
  EXPECT_EQ(mem1->mapped_ptr() + kOffset, mem2->mapped_ptr());

  // Allocate 2 buffers, one from the original allocation, and one from the
  // sub-allocation.
  auto buf1 =
      Buffer::New(recycler, mem1, kBufferUsageFlags, kBufferSize, kOffset);
  auto buf2 = Buffer::New(recycler, mem2, kBufferUsageFlags, kBufferSize, 0);
  EXPECT_EQ(buf1->host_ptr(), buf2->host_ptr());
  EXPECT_EQ(mem2->mapped_ptr(), buf2->host_ptr());
}

}  // namespace escher
