// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/dma-buffer/buffer.h>
#include <lib/mmio/mmio.h>

#include <fake-dma-buffer/fake-dma-buffer.h>
#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

namespace ddk_fake_test {

const zx::bti kFakeBti(42);
constexpr auto kAlignmentLog2 = 12;

TEST(FakeDmaBuffer, ContiguousBufferMultiPage) {
  auto factory = ddk_fake::CreateBufferFactory();
  std::unique_ptr<dma_buffer::ContiguousBuffer> buffer;
  ASSERT_EQ(factory->CreateContiguous(kFakeBti, ZX_PAGE_SIZE * 2, 0, &buffer),
            ZX_ERR_NOT_SUPPORTED);
}

TEST(FakeDmaBuffer, ContiguousBuffer) {
  auto factory = ddk_fake::CreateBufferFactory();
  std::unique_ptr<dma_buffer::ContiguousBuffer> buffer;
  ASSERT_OK(factory->CreateContiguous(kFakeBti, ZX_PAGE_SIZE, kAlignmentLog2, &buffer));
  ASSERT_EQ(ddk_fake::PhysToVirt(buffer->phys()), buffer->virt());
  auto& page = ddk_fake::GetPage(buffer->phys());
  ASSERT_EQ(page.alignment_log2, kAlignmentLog2);
  ASSERT_EQ(page.bti, kFakeBti.get());
  ASSERT_TRUE(page.contiguous);
  ASSERT_TRUE(page.enable_cache);
  ASSERT_EQ(page.size, ZX_PAGE_SIZE);
}

TEST(FakeDmaBuffer, UncachedPagedBuffer) {
  auto factory = ddk_fake::CreateBufferFactory();
  std::unique_ptr<dma_buffer::PagedBuffer> buffer;
  constexpr auto kPageCount = 4;
  ASSERT_OK(factory->CreatePaged(kFakeBti, ZX_PAGE_SIZE * kPageCount, false, &buffer));
  for (size_t i = 0; i < kPageCount; i++) {
    ASSERT_EQ(ddk_fake::PhysToVirt(buffer->phys()[i]),
              static_cast<uint8_t*>(buffer->virt()) + (i * ZX_PAGE_SIZE));
    auto& page = ddk_fake::GetPage(buffer->phys()[i]);
    ASSERT_EQ(page.bti, kFakeBti.get());
    ASSERT_FALSE(page.contiguous);
    ASSERT_FALSE(page.enable_cache);
    ASSERT_EQ(page.size, ZX_PAGE_SIZE * kPageCount);
  }
}

TEST(FakeDmaBuffer, CachedPagedBuffer) {
  auto factory = ddk_fake::CreateBufferFactory();
  std::unique_ptr<dma_buffer::PagedBuffer> buffer;
  constexpr auto kPageCount = 4;
  ASSERT_OK(factory->CreatePaged(kFakeBti, ZX_PAGE_SIZE * kPageCount, true, &buffer));
  for (size_t i = 0; i < kPageCount; i++) {
    ASSERT_EQ(ddk_fake::PhysToVirt(buffer->phys()[i]),
              static_cast<uint8_t*>(buffer->virt()) + (i * ZX_PAGE_SIZE));
    auto& page = ddk_fake::GetPage(buffer->phys()[i]);
    ASSERT_EQ(page.bti, kFakeBti.get());
    ASSERT_FALSE(page.contiguous);
    ASSERT_TRUE(page.enable_cache);
    ASSERT_EQ(page.size, ZX_PAGE_SIZE * kPageCount);
  }
}

}  // namespace ddk_fake_test
