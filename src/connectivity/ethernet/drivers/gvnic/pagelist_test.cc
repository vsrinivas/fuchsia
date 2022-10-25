// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/pagelist.h"

#include <lib/dma-buffer/buffer.h>
#include <lib/fake-bti/bti.h>

#include <unordered_set>

#include <fake-dma-buffer/fake-dma-buffer.h>
#include <zxtest/zxtest.h>

#include "src/connectivity/ethernet/drivers/gvnic/bigendian.h"

using namespace std;

namespace gvnic {

TEST(PagelistTest, BasicUsage) {
  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  std::unique_ptr<dma_buffer::BufferFactory> factory(ddk_fake::CreateBufferFactory());
  EXPECT_TRUE(factory);

  std::unique_ptr<dma_buffer::ContiguousBuffer> scratch;
  EXPECT_OK(factory->CreateContiguous(bti, zx_system_get_page_size(), 0, &scratch));

  // Just create the pagelist, and verify that it has pages with addresses.
  PageList page_list(factory, bti, scratch);
  EXPECT_NOT_NULL(page_list.pages());
  EXPECT_NOT_NULL(page_list.pages()->virt());
  EXPECT_NOT_NULL(page_list.pages()->phys());
}

TEST(PagelistTest, ScratchIsPopulated) {
  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  std::unique_ptr<dma_buffer::BufferFactory> factory(ddk_fake::CreateBufferFactory());
  std::unique_ptr<dma_buffer::ContiguousBuffer> scratch;
  EXPECT_OK(factory->CreateContiguous(bti, zx_system_get_page_size(), 0, &scratch));

  // Zero out the scratch page. (Set all entries to 0)
  memset(scratch->virt(), 0, scratch->size());

  PageList page_list(factory, bti, scratch);

  // Check that they are not 0 anymore.
  auto const scratch_addrs = reinterpret_cast<BigEndian<uint64_t>*>(scratch->virt());
  const auto expected_length = scratch->size() / sizeof(uint64_t);
  for (uint32_t i = 0; i < expected_length; i++) {
    EXPECT_NE(0UL, scratch_addrs[i]);
  }
}

TEST(PagelistTest, IdsIncrement) {
  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  std::unique_ptr<dma_buffer::BufferFactory> factory(ddk_fake::CreateBufferFactory());
  std::unique_ptr<dma_buffer::ContiguousBuffer> scratch;
  EXPECT_OK(factory->CreateContiguous(bti, zx_system_get_page_size(), 0, &scratch));

  // The ids wont nesecarily start at 0 (since IDs are allocated by other tests), but consecutively
  // created pagelists should have ids that are contiguous.
  PageList pl0(factory, bti, scratch), pl1(factory, bti, scratch), pl2(factory, bti, scratch),
      pl3(factory, bti, scratch), pl4(factory, bti, scratch), pl5(factory, bti, scratch);
  EXPECT_EQ(pl0.id() + 1, pl1.id());
  EXPECT_EQ(pl1.id() + 1, pl2.id());
  EXPECT_EQ(pl2.id() + 1, pl3.id());
  EXPECT_EQ(pl3.id() + 1, pl4.id());
  EXPECT_EQ(pl4.id() + 1, pl5.id());
}

TEST(PagelistTest, CheckLengths) {
  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  std::unique_ptr<dma_buffer::BufferFactory> factory(ddk_fake::CreateBufferFactory());
  std::unique_ptr<dma_buffer::ContiguousBuffer> scratch;
  EXPECT_OK(factory->CreateContiguous(bti, zx_system_get_page_size(), 0, &scratch));

  PageList page_list(factory, bti, scratch);

  // The pagelist length and the length of the actual pages should be consistent.
  const auto expected_length = scratch->size() / sizeof(uint64_t);
  EXPECT_EQ(expected_length, page_list.length());
  EXPECT_EQ(expected_length * zx_system_get_page_size(), page_list.pages()->size());
}

TEST(PagelistTest, AddrsAreUnique) {
  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  std::unique_ptr<dma_buffer::BufferFactory> factory(ddk_fake::CreateBufferFactory());
  std::unique_ptr<dma_buffer::ContiguousBuffer> scratch;
  EXPECT_OK(factory->CreateContiguous(bti, zx_system_get_page_size(), 0, &scratch));

  PageList page_list(factory, bti, scratch);

  // The addresses should all be unique, no duplicates.
  const auto expected_length = scratch->size() / sizeof(uint64_t);
  unordered_set<zx_paddr_t> pl_addr_set;
  pl_addr_set.insert(page_list.pages()->phys(), page_list.pages()->phys() + expected_length);
  EXPECT_EQ(expected_length, pl_addr_set.size());
}

TEST(PagelistTest, ScratchMatchesPagesButBigEndian) {
  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  std::unique_ptr<dma_buffer::BufferFactory> factory(ddk_fake::CreateBufferFactory());
  std::unique_ptr<dma_buffer::ContiguousBuffer> scratch;
  EXPECT_OK(factory->CreateContiguous(bti, zx_system_get_page_size(), 0, &scratch));

  PageList page_list(factory, bti, scratch);

  // The scratch page should contain bigendian versions of the addresses in pages.
  const auto expected_length = scratch->size() / sizeof(uint64_t);
  auto const scratch_addrs = reinterpret_cast<BigEndian<uint64_t>*>(scratch->virt());
  auto const pages_addrs = page_list.pages()->phys();
  for (uint32_t i = 0; i < expected_length; i++) {
    EXPECT_EQ(pages_addrs[i], scratch_addrs[i]);
  }
}

}  // namespace gvnic
