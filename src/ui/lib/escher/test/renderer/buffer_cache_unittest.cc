// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/buffer_cache.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/test/common/gtest_escher.h"

namespace escher {

VK_TEST(BufferCache, CreateDestroyCache) {
  auto escher = test::GetEscher()->GetWeakPtr();

  fxl::WeakPtr<BufferCache> weak_cache;
  {
    BufferCache cache(escher);
    weak_cache = cache.GetWeakPtr();
  }

  EXPECT_FALSE(weak_cache);
}

VK_TEST(BufferCache, CreateBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  BufferCache buffer_cache(escher);

  vk::DeviceSize requested_size = 256;
  BufferPtr buffer = buffer_cache.NewHostBuffer(requested_size);

  EXPECT_EQ(requested_size, buffer->size());
}

VK_TEST(BufferCache, RecycleBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  BufferCache buffer_cache(escher);
  vk::DeviceSize requested_size = 512;
  BufferPtr buffer = buffer_cache.NewHostBuffer(requested_size);
  uint64_t buffer_id = buffer->uid();

  // Recycle the buffer and request a new buffer of equal or smaller size.
  buffer_cache.RecycleResource(std::unique_ptr<Resource>(static_cast<Resource*>(buffer.get())));
  vk::DeviceSize requested_size2 = 256;
  BufferPtr buffer2 = buffer_cache.NewHostBuffer(requested_size2);

  // The first buffer should have been recycled by the cache.
  EXPECT_EQ(buffer_id, buffer2->uid());
}

VK_TEST(BufferCache, SmallerBufferShouldNotRecycle) {
  auto escher = test::GetEscher()->GetWeakPtr();

  // We create a buffer of size 127 and a buffer of size 128.
  //
  // In most cases for the first request we will generate a buffer of
  // size > 127; however after recycling this buffer, we still cannot use that
  // for buffers of size 128, as the requested buffer size should be less than
  // or equal to the Vulkan buffer size.
  BufferCache buffer_cache(escher);
  vk::DeviceSize requested_size_1 = 127;
  vk::DeviceSize requested_size_2 = 128;

  BufferPtr buffer1 = buffer_cache.NewHostBuffer(requested_size_1);
  uint64_t buffer1_id = buffer1->uid();
  EXPECT_EQ(buffer1->size(), requested_size_1);

  // Recycle buffer 1.
  buffer1.reset();

  BufferPtr buffer2 = buffer_cache.NewHostBuffer(requested_size_2);
  uint64_t buffer2_id = buffer2->uid();
  EXPECT_EQ(buffer2->size(), requested_size_2);

  EXPECT_TRUE(buffer1_id != buffer2_id);
}

VK_TEST(BufferCache, SlightlyLargerBufferShouldRecycle) {
  auto escher = test::GetEscher()->GetWeakPtr();

  // We create a buffer of size 256 and then recycle it. Then when we create a
  // buffer of size 254 we will reuse the previous buffer.
  BufferCache buffer_cache(escher);
  vk::DeviceSize requested_size_1 = 256;
  vk::DeviceSize requested_size_2 = 254;

  BufferPtr buffer1 = buffer_cache.NewHostBuffer(requested_size_1);
  uint64_t buffer1_id = buffer1->uid();
  vk::DeviceSize buffer1_size = buffer1->size();

  // Recycle buffer 1.
  buffer1.reset();

  BufferPtr buffer2 = buffer_cache.NewHostBuffer(requested_size_2);
  uint64_t buffer2_id = buffer2->uid();

  EXPECT_EQ(buffer1_size, buffer2->size());
  EXPECT_TRUE(buffer1_id == buffer2_id);
}

VK_TEST(BufferCache, DontRecycleLargeBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  BufferCache buffer_cache(escher);
  vk::DeviceSize requested_size = 512;
  BufferPtr buffer = buffer_cache.NewHostBuffer(requested_size);
  uint64_t buffer_id = buffer->uid();

  // Recycle the buffer and request a new buffer of less than half the size.
  EXPECT_EQ(0U, buffer_cache.free_buffer_count());
  buffer = nullptr;
  EXPECT_EQ(1U, buffer_cache.free_buffer_count());
  vk::DeviceSize requested_size2 = requested_size / 4;
  BufferPtr buffer2 = buffer_cache.NewHostBuffer(requested_size2);

  // The first buffer should not have been recycled by the cache.
  EXPECT_NE(buffer_id, buffer2->uid());
}

VK_TEST(BufferCache, RecycleMany) {
  auto escher = test::GetEscher()->GetWeakPtr();
  BufferCache buffer_cache(escher);
  vk::DeviceSize requested_size = 1024 * 512;
  BufferPtr big_buffer = buffer_cache.NewHostBuffer(requested_size);
  BufferPtr big_buffer2 = buffer_cache.NewHostBuffer(requested_size);
  BufferPtr big_buffer3 = buffer_cache.NewHostBuffer(requested_size * 2);
  uint64_t big_buffer_id = big_buffer->uid();
  uint64_t big_buffer2_id = big_buffer2->uid();
  uint64_t big_buffer3_id = big_buffer3->uid();
  if (big_buffer_id == big_buffer2_id || big_buffer_id == big_buffer3_id ||
      big_buffer2_id == big_buffer3_id) {
    // TODO(fxbug.dev/23753) It seems that the allocator is allocating garbage
    // memory, and then later filling multiple BufferPtrs with the same
    // buffer, allocated later. Allocating buffers with the same ID will
    // crash the BufferCache (as the invariant that buffer IDs are unique)
    // must be held. When this case is hit, error out early of the test.
    // Remove this early return when SCN-526 is resolved.
    FX_LOGS(ERROR) << "Error allocating memory, aborting test!";
    return;
  }

  // Recycle the buffers. The third buffer should flush the cache.
  EXPECT_EQ(0U, buffer_cache.free_buffer_count());
  big_buffer = nullptr;
  big_buffer2 = nullptr;
  EXPECT_EQ(2U, buffer_cache.free_buffer_count());
  big_buffer3 = nullptr;

  // Requesting a buffer should create a new buffer since the cache should
  // have been flushed by the third buffer, and it is too big to use for this
  // fourth buffer.
  BufferPtr big_buffer4 = buffer_cache.NewHostBuffer(requested_size / 2);
  EXPECT_NE(big_buffer3_id, big_buffer4->uid());
  EXPECT_NE(big_buffer2_id, big_buffer4->uid());
  EXPECT_NE(big_buffer_id, big_buffer4->uid());

  // Request a buffer that should use the recycled big_buffer3.
  BufferPtr big_buffer5 = buffer_cache.NewHostBuffer(requested_size);
  EXPECT_EQ(big_buffer3_id, big_buffer5->uid());
}

}  // namespace escher
