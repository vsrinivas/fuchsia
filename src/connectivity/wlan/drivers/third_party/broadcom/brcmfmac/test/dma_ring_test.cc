// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"

#include <lib/fake-bti/bti.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <atomic>
#include <cstring>
#include <memory>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Test different parameters to DmaRing creation.
TEST(DmaRingTest, CreationParameters) {
  constexpr size_t kVmoSize = 1024 * 8;
  constexpr size_t kItemSize = 512;

  zx_handle_t fake_bti_handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_OK, fake_bti_create(&fake_bti_handle));
  zx::bti bti(fake_bti_handle);
  std::unique_ptr<DmaBuffer> dma_buffer;

  volatile std::atomic<uint16_t> read_index = {};
  volatile std::atomic<uint16_t> write_index = {};
  volatile std::atomic<uint32_t> write_signal = {};
  std::unique_ptr<ReadDmaRing> read_ring;
  std::unique_ptr<WriteDmaRing> write_ring;

  // Expect that we can create DMA rings up to the VMO buffer size.
  for (size_t i = 0; i <= kVmoSize / kItemSize; ++i) {
    ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
    read_index.store(5);
    write_index.store(42);
    EXPECT_EQ(ZX_OK, ReadDmaRing::Create(std::move(dma_buffer), kItemSize, i, &read_index,
                                         &write_index, &read_ring));
    EXPECT_EQ(0u, read_index.load());
    EXPECT_EQ(0u, write_index.load());
    EXPECT_NE(nullptr, read_ring);
    read_ring.reset();

    ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
    read_index.store(5);
    write_index.store(42);
    write_signal.store(0);
    EXPECT_EQ(ZX_OK, WriteDmaRing::Create(std::move(dma_buffer), kItemSize, i, &read_index,
                                          &write_index, &write_signal, &write_ring));
    EXPECT_EQ(0u, read_index.load());
    EXPECT_EQ(0u, write_index.load());
    EXPECT_EQ(0u, write_signal.load());
    EXPECT_NE(nullptr, write_ring);
    write_ring.reset();
  }

  // Expect that creation will fail if we exceed the VMO buffer size.
  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  EXPECT_NE(ZX_OK, ReadDmaRing::Create(std::move(dma_buffer), kItemSize, (kVmoSize / kItemSize) + 1,
                                       &read_index, &write_index, &read_ring));
  EXPECT_EQ(nullptr, read_ring);
  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  EXPECT_NE(ZX_OK,
            WriteDmaRing::Create(std::move(dma_buffer), kItemSize, (kVmoSize / kItemSize) + 1,
                                 &read_index, &write_index, &write_signal, &write_ring));
  EXPECT_EQ(nullptr, write_ring);

  fake_bti_destroy(bti.release());
}

// Test reading from a ReadDmaRing.
TEST(DmaRingTest, ReadTest) {
  constexpr size_t kVmoSize = 1024 * 8;
  constexpr size_t kItemCount = kVmoSize / sizeof(uint32_t);

  zx_handle_t fake_bti_handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_OK, fake_bti_create(&fake_bti_handle));
  zx::bti bti(fake_bti_handle);
  zx::vmar vmar;
  zx_vaddr_t vmar_address = 0;
  std::unique_ptr<DmaBuffer> dma_buffer;
  uintptr_t dma_buffer_address = 0;

  ASSERT_EQ(ZX_OK,
            zx::vmar::root_self()->allocate(0, kVmoSize, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE,
                                            &vmar, &vmar_address));
  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  EXPECT_EQ(ZX_OK, dma_buffer->Map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &dma_buffer_address));
  ASSERT_NE(0u, dma_buffer_address);

  void* dma_buffer_data = reinterpret_cast<void*>(dma_buffer_address);
  uint32_t buffer_data[kItemCount];
  for (uint32_t i = 0; i < kItemCount; ++i) {
    buffer_data[i] = i;
  }
  EXPECT_NE(nullptr, dma_buffer_data);
  std::memcpy(dma_buffer_data, buffer_data, sizeof(buffer_data));

  volatile std::atomic<uint16_t> read_index(5);
  volatile std::atomic<uint16_t> write_index(42);
  std::unique_ptr<ReadDmaRing> read_ring;
  EXPECT_EQ(ZX_OK, ReadDmaRing::Create(std::move(dma_buffer), sizeof(uint32_t), kItemCount,
                                       &read_index, &write_index, &read_ring));

  // Iterate over the entire ring, reading one item at a time.
  for (size_t i = 0; i < kItemCount; ++i) {
    EXPECT_EQ(i, read_index.load());
    EXPECT_EQ(i, write_index.load());
    const void* read_buffer = nullptr;
    uint32_t value = 0;

    // No elements in the ring.
    EXPECT_NE(ZX_OK, read_ring->MapRead(1, &read_buffer));
    EXPECT_NE(ZX_OK, read_ring->CommitRead(1));
    EXPECT_EQ(i, read_index.load());
    EXPECT_EQ(i, write_index.load());
    EXPECT_EQ(0, read_ring->GetAvailableReads());

    // Simulate a HW put of an element to the ring.
    write_index.store((write_index.load() + 1) % kItemCount);

    // Now expect to be able to read it back.
    EXPECT_EQ(1, read_ring->GetAvailableReads());
    EXPECT_EQ(ZX_OK, read_ring->MapRead(1, &read_buffer));
    std::memcpy(&value, read_buffer, sizeof(value));
    EXPECT_EQ(i, value);
    EXPECT_EQ(i, read_index.load());
    EXPECT_EQ((i + 1) % kItemCount, write_index.load());

    // Fail an overcommit.
    EXPECT_NE(ZX_OK, read_ring->CommitRead(2));
    EXPECT_EQ(i, read_index.load());
    EXPECT_EQ((i + 1) % kItemCount, write_index.load());

    // Now commit.
    EXPECT_EQ(ZX_OK, read_ring->CommitRead(1));
    EXPECT_EQ(0, read_ring->GetAvailableReads());
    EXPECT_EQ((i + 1) % kItemCount, read_index.load());
    EXPECT_EQ((i + 1) % kItemCount, write_index.load());
  }

  // The entire ring has been iterated over.
  EXPECT_EQ(0u, read_index.load());
  EXPECT_EQ(0u, write_index.load());

  vmar.destroy();
  fake_bti_destroy(bti.release());
}

// Test writing to a WriteDmaRing.
TEST(DmaRingTest, WriteTest) {
  constexpr size_t kVmoSize = 1024 * 8;
  constexpr size_t kItemCount = kVmoSize / sizeof(uint32_t);

  zx_handle_t fake_bti_handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_OK, fake_bti_create(&fake_bti_handle));
  zx::bti bti(fake_bti_handle);
  zx::vmar vmar;
  zx_vaddr_t vmar_address = 0;
  std::unique_ptr<DmaBuffer> dma_buffer;
  uintptr_t dma_buffer_address = 0;

  ASSERT_EQ(ZX_OK,
            zx::vmar::root_self()->allocate(0, kVmoSize, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE,
                                            &vmar, &vmar_address));
  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kVmoSize, &dma_buffer));
  EXPECT_EQ(ZX_OK, dma_buffer->Map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &dma_buffer_address));
  ASSERT_NE(0u, dma_buffer_address);

  void* dma_buffer_data = reinterpret_cast<void*>(dma_buffer_address);
  uint32_t buffer_data[kItemCount];
  for (uint32_t i = 0; i < kItemCount; ++i) {
    buffer_data[i] = 0;
  }
  std::memcpy(dma_buffer_data, buffer_data, sizeof(buffer_data));

  volatile std::atomic<uint16_t> read_index(5);
  volatile std::atomic<uint16_t> write_index(42);
  volatile std::atomic<uint32_t> write_signal(0);
  std::unique_ptr<WriteDmaRing> write_ring;
  EXPECT_EQ(ZX_OK, WriteDmaRing::Create(std::move(dma_buffer), sizeof(uint32_t), kItemCount,
                                        &read_index, &write_index, &write_signal, &write_ring));

  // Write over almost the entire buffer.
  EXPECT_EQ(kItemCount - 1, write_ring->GetAvailableWrites());
  EXPECT_EQ(0u, read_index.load());
  EXPECT_EQ(0u, write_index.load());
  EXPECT_EQ(0u, write_signal.load());
  void* write_buffer = nullptr;
  EXPECT_NE(ZX_OK, write_ring->MapWrite(kItemCount, &write_buffer));
  EXPECT_EQ(ZX_OK, write_ring->MapWrite(kItemCount - 1, &write_buffer));
  std::memset(write_buffer, 0xFF, (kItemCount - 1) * sizeof(uint32_t));
  EXPECT_EQ(ZX_OK, write_ring->CommitWrite(1));
  EXPECT_EQ(0u, read_index.load());
  EXPECT_EQ(1u, write_index.load());
  EXPECT_EQ(1u, write_signal.load());
  write_signal.store(0);
  EXPECT_NE(ZX_OK, write_ring->CommitWrite(kItemCount - 1));
  EXPECT_EQ(0u, read_index.load());
  EXPECT_EQ(1u, write_index.load());
  EXPECT_EQ(0u, write_signal.load());
  EXPECT_EQ(ZX_OK, write_ring->CommitWrite(kItemCount - 2));
  EXPECT_EQ(0u, read_index.load());
  EXPECT_EQ(kItemCount - 1, write_index.load());
  EXPECT_EQ(1u, write_signal.load());
  write_signal.store(0);
  std::memcpy(buffer_data, dma_buffer_data, sizeof(buffer_data));
  for (uint32_t i = 0; i < kItemCount; ++i) {
    if (i == kItemCount - 1) {
      EXPECT_EQ(0u, buffer_data[i]);
    } else {
      EXPECT_EQ(0xFFFFFFFF, buffer_data[i]);
    }
  }

  // Write the last entry.
  read_index.store(1);
  EXPECT_EQ(1, write_ring->GetAvailableWrites());
  EXPECT_EQ(ZX_OK, write_ring->CommitWrite(1));
  EXPECT_EQ(1u, read_index.load());
  EXPECT_EQ(0u, write_index.load());
  EXPECT_EQ(1u, write_signal.load());
  write_signal.store(0);

  // Iterate over the entire ring, writing one item at a time.
  for (size_t i = 0; i < kItemCount; ++i) {
    uint32_t value = 0;
    void* write_buffer = nullptr;

    // No space available in the ring.
    EXPECT_EQ(0, write_ring->GetAvailableWrites());
    EXPECT_NE(ZX_OK, write_ring->MapWrite(1, &write_buffer));
    EXPECT_NE(ZX_OK, write_ring->CommitWrite(1));
    EXPECT_EQ((i + 1) % kItemCount, read_index.load());
    EXPECT_EQ(i, write_index.load());
    EXPECT_EQ(0u, write_signal.load());

    // Simulate a HW get of an element to the ring.
    read_index.store((read_index.load() + 1) % kItemCount);

    // Now expect to be able to write something.
    EXPECT_EQ(ZX_OK, write_ring->MapWrite(1, &write_buffer));
    value = 0xDEADBEEF;
    std::memcpy(write_buffer, &value, sizeof(value));
    EXPECT_EQ((i + 2) % kItemCount, read_index.load());
    EXPECT_EQ(i, write_index.load());
    EXPECT_EQ(0u, write_signal.load());

    // Fail an overcommit.
    EXPECT_NE(ZX_OK, write_ring->CommitWrite(2));
    EXPECT_EQ((i + 2) % kItemCount, read_index.load());
    EXPECT_EQ(i, write_index.load());
    EXPECT_EQ(0u, write_signal.load());

    // Now commit.
    EXPECT_EQ(ZX_OK, write_ring->CommitWrite(1));
    EXPECT_EQ(0, write_ring->GetAvailableWrites());
    EXPECT_EQ((i + 2) % kItemCount, read_index.load());
    EXPECT_EQ((i + 1) % kItemCount, write_index.load());
    EXPECT_EQ(1u, write_signal.load());
    write_signal.store(0);
  }

  // The entire ring has been iterated over.
  EXPECT_EQ(1u, read_index.load());
  EXPECT_EQ(0u, write_index.load());
  EXPECT_EQ(0u, write_signal.load());

  std::memcpy(buffer_data, dma_buffer_data, sizeof(buffer_data));
  for (uint32_t i = 0; i < kItemCount; ++i) {
    EXPECT_EQ(0xDEADBEEF, buffer_data[i]);
  }

  vmar.destroy();
  fake_bti_destroy(bti.release());
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
