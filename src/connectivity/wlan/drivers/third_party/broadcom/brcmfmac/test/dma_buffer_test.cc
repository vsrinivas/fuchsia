// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"

#include <lib/fake-bti/bti.h>
#include <lib/zx/bti.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstring>
#include <memory>
#include <vector>

#include "gtest/gtest.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Test different parameters to DmaBuffer creation.
TEST(DmaBufferTest, CreationParameters) {
  constexpr size_t kLargeBufferSize = 1024 * 8;  // Page-aligned.
  constexpr size_t kSmallBufferSize = 512;       // Not page-aligned.
  constexpr size_t kUnalignedBufferSize = 4127;  // Not page-aligned (and also prime).

  zx::bti bti;
  ASSERT_EQ(ZX_OK, fake_bti_create(bti.reset_and_get_address()));
  std::unique_ptr<DmaBuffer> dma_buffer;

  EXPECT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kLargeBufferSize, &dma_buffer));
  EXPECT_LE(kLargeBufferSize, dma_buffer->size());

  EXPECT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kSmallBufferSize, &dma_buffer));
  EXPECT_LE(kSmallBufferSize, dma_buffer->size());

  EXPECT_EQ(ZX_OK,
            DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kUnalignedBufferSize, &dma_buffer));
  EXPECT_LE(kUnalignedBufferSize, dma_buffer->size());
}

// Test that we can write and read back data written on the CPU to the DmaBuffer.
TEST(DmaBufferTest, ReadWriteTest) {
  constexpr size_t kBufferSize = 1024 * 8;

  zx::bti bti;
  ASSERT_EQ(ZX_OK, fake_bti_create(bti.reset_and_get_address()));
  std::unique_ptr<DmaBuffer> dma_buffer;

  ASSERT_EQ(ZX_OK, DmaBuffer::Create(bti, ZX_CACHE_POLICY_CACHED, kBufferSize, &dma_buffer));
  EXPECT_EQ(0u, dma_buffer->address());
  ASSERT_EQ(ZX_OK, dma_buffer->Map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE));
  ASSERT_NE(0u, dma_buffer->address());

  std::vector<char> write_data(kBufferSize, '\x80');
  std::memcpy(reinterpret_cast<void*>(dma_buffer->address()), write_data.data(), write_data.size());
  std::vector<char> read_data(kBufferSize, '\0');
  std::memcpy(read_data.data(), reinterpret_cast<void*>(dma_buffer->address()), kBufferSize);
  EXPECT_TRUE(std::equal(write_data.begin(), write_data.end(), read_data.begin()));

  EXPECT_EQ(ZX_OK, dma_buffer->Unmap());
  EXPECT_EQ(0u, dma_buffer->address());
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
