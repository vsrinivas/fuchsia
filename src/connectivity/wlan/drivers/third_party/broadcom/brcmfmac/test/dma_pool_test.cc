// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_pool.h"

#include <lib/fake-bti/bti.h>
#include <lib/zx/bti.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Test different parameters to DmaPool creation.
TEST(DmaPoolTest, CreationParameters) {
  constexpr size_t kVmoSize = 1024 * 8;
  constexpr size_t kItemSize = 512;
  zx_handle_t fake_bti_handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_OK, fake_bti_create(&fake_bti_handle));
  zx::bti bti(fake_bti_handle);
  std::unique_ptr<DmaBuffer> dma_buffer;
  std::unique_ptr<DmaPool> dma_pool;

  // Expect that we can create DMA pools up to the VMO buffer size.
  for (size_t i = 1; i <= kVmoSize / kItemSize; ++i) {
    ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
    EXPECT_EQ(ZX_OK, DmaPool::Create(kItemSize, i, std::move(dma_buffer), &dma_pool));
    EXPECT_EQ(kItemSize, dma_pool->buffer_size());
    EXPECT_EQ(i, static_cast<size_t>(dma_pool->buffer_count()));
  }

  // Expect that DMA pools of zero or greater than the VMO buffer size fail.
  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  EXPECT_NE(ZX_OK,
            DmaPool::Create(kItemSize, kVmoSize / kItemSize + 1, std::move(dma_buffer), &dma_pool));
  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  EXPECT_NE(ZX_OK, DmaPool::Create(kItemSize, 0, std::move(dma_buffer), &dma_pool));

  fake_bti_destroy(bti.release());
}

// Test that we can use allocate, free, release, and acquire released Buffer instances.
TEST(DmaPoolTest, AllocateAcquire) {
  constexpr size_t kVmoSize = 4096;
  constexpr size_t kItemSize = 512;
  zx_handle_t fake_bti_handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_OK, fake_bti_create(&fake_bti_handle));
  zx::bti bti(fake_bti_handle);
  std::unique_ptr<DmaBuffer> dma_buffer;
  std::unique_ptr<DmaPool> dma_pool;

  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  ASSERT_EQ(ZX_OK,
            DmaPool::Create(kItemSize, kVmoSize / kItemSize, std::move(dma_buffer), &dma_pool));

  // Allocate all the buffers.
  std::vector<DmaPool::Buffer> buffers;
  for (size_t i = 0; i < kVmoSize / kItemSize; ++i) {
    buffers.emplace_back();
    EXPECT_EQ(ZX_OK, dma_pool->Allocate(&buffers.back())) << "i=" << i;
  }

  // Fail to allocate when full.
  DmaPool::Buffer buffer;
  EXPECT_NE(ZX_OK, dma_pool->Allocate(&buffer));

  // If we return one buffer, we should be able to get it back.
  const int last_index = buffers.back().index();
  buffers.pop_back();
  EXPECT_EQ(ZX_OK, dma_pool->Allocate(&buffer));
  EXPECT_EQ(last_index, buffer.index());

  // We cannot acquire a buffer that has been allocated.
  DmaPool::Buffer acquired_buffer;
  EXPECT_NE(ZX_OK, dma_pool->Acquire(last_index, &acquired_buffer));
  EXPECT_FALSE(acquired_buffer.is_valid());

  // Write some data to the buffer.
  std::vector<char> write_data(dma_pool->buffer_size(), '\x80');
  void* write_pointer = nullptr;
  EXPECT_EQ(ZX_OK, buffer.MapWrite(write_data.size() * sizeof(write_data[0]), &write_pointer));
  std::memcpy(write_pointer, write_data.data(), write_data.size() * sizeof(write_data[0]));

  // But if we release it, we can.
  buffer.Release();
  EXPECT_FALSE(buffer.is_valid());
  EXPECT_EQ(ZX_OK, dma_pool->Acquire(last_index, &acquired_buffer));
  EXPECT_EQ(last_index, acquired_buffer.index());
  EXPECT_TRUE(acquired_buffer.is_valid());

  // Check that we can read the data back.
  std::vector<char> read_data(dma_pool->buffer_size(), '\0');
  const void* read_pointer = nullptr;
  EXPECT_EQ(ZX_OK, acquired_buffer.MapRead(read_data.size() * sizeof(read_data[0]), &read_pointer));
  std::memcpy(read_data.data(), read_pointer, read_data.size() * sizeof(read_data[0]));
  EXPECT_EQ(0, std::memcmp(write_data.data(), read_data.data(),
                           write_data.size() * sizeof(write_data[0])));

  // Return everything.  First a third of the buffers, then the rest (to disorder them).
  for (size_t i = 0; i < buffers.size(); i += 3) {
    buffers[i].Reset();
  }
  buffers.clear();
  buffer.Reset();
  acquired_buffer.Reset();

  // Allocate and release half the buffers.
  std::unordered_set<int> released_buffers;
  for (int i = 0; i < dma_pool->buffer_count() / 2; ++i) {
    EXPECT_EQ(ZX_OK, dma_pool->Allocate(&buffer)) << "i=" << i;
    EXPECT_TRUE(released_buffers.insert(buffer.index()).second) << "i=" << i;
    buffer.Release();
  }

  // Make sure we can acquire the released buffers, but not the others.
  for (int i = 0; i < dma_pool->buffer_count(); ++i) {
    const bool buffer_is_released = (released_buffers.find(i) != released_buffers.end());
    EXPECT_EQ(buffer_is_released, dma_pool->Acquire(i, &buffer) == ZX_OK) << "i=" << i;
  }

  fake_bti_destroy(bti.release());
}

// This is a smoke test for the thread safety of DmaPool.  We create Lots Of Threads and make them
// do Lots Of Things, simultaneously.
TEST(DmaPoolTest, ThreadSafety) {
  constexpr size_t kVmoSize = 16 * 1024;
  constexpr size_t kItemSize = 32;
  constexpr int kThreadCount = 16;
  constexpr int kIterationCount = 8 * 1024;
  zx_handle_t fake_bti_handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_OK, fake_bti_create(&fake_bti_handle));
  zx::bti bti(fake_bti_handle);
  std::unique_ptr<DmaBuffer> dma_buffer;
  std::unique_ptr<DmaPool> dma_pool;

  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  ASSERT_EQ(ZX_OK,
            DmaPool::Create(kItemSize, kVmoSize / kItemSize, std::move(dma_buffer), &dma_pool));

  // Allocate half the buffers, then return a random half of them.
  std::vector<DmaPool::Buffer> buffers(dma_pool->buffer_count());
  for (size_t i = 0; i < buffers.size(); ++i) {
    EXPECT_EQ(ZX_OK, dma_pool->Allocate(&buffers[i])) << "i=" << i;
  }
  std::shuffle(buffers.begin(), buffers.end(), std::mt19937());
  buffers.resize(buffers.size() / 2);

  // Make a bunch of threads that randomly allocate or return buffers.
  std::vector<std::thread> threads;
  for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
    threads.emplace_back([thread_index, &dma_pool]() {
      std::mt19937 rand(thread_index);
      std::vector<DmaPool::Buffer> buffers;
      for (int i = 0; i < kIterationCount; ++i) {
        // Randomly choose to allocate or free a Buffer.
        if (std::uniform_int_distribution<int>(0, 1)(rand)) {
          buffers.emplace_back();
          dma_pool->Allocate(&buffers.back());
        } else if (buffers.size() > 0) {
          size_t erase_index = std::uniform_int_distribution<size_t>(0, buffers.size() - 1)(rand);
          buffers.erase(buffers.begin() + erase_index);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();

  // Release the rest of the buffers, and make sure we can allocate them all again.
  buffers.clear();
  buffers.resize(dma_pool->buffer_count());
  for (size_t i = 0; i < buffers.size(); ++i) {
    EXPECT_EQ(ZX_OK, dma_pool->Allocate(&buffers[i])) << "i=" << i;
  }
  DmaPool::Buffer buffer;
  EXPECT_NE(ZX_OK, dma_pool->Allocate(&buffer));

  fake_bti_destroy(bti.release());
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
