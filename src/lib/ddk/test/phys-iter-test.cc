// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ddk/phys-iter.h"

#include <iterator>
#include <utility>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

namespace {

TEST(PhyIterTests, EmptyIteratorTest) {
  ddk::PhysIter phys_iter(phys_iter_buffer_t{}, 0);
  EXPECT_TRUE(phys_iter.begin() == phys_iter.end());
}

TEST(PhyIterTests, SimpleIterationTest) {
  constexpr zx_paddr_t kPhysList[] = {
      2 * ZX_PAGE_SIZE,
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = ZX_PAGE_SIZE,
      .vmo_offset = 0,
      .sg_list = nullptr,
      .sg_count = 0,
  };

  ddk::PhysIter phys_iter(kIterBuffer, 0);
  auto iter = phys_iter.begin();
  const auto end = phys_iter.end();

  EXPECT_TRUE(iter != end);
  auto [paddr, size] = *iter;
  EXPECT_EQ(paddr, 2 * ZX_PAGE_SIZE);
  EXPECT_EQ(size, ZX_PAGE_SIZE);

  ++iter;
  EXPECT_TRUE(iter == end);

  size_t count = 0;
  for (auto [paddr, size] : phys_iter) {
    ++count;
    EXPECT_EQ(paddr, 2 * ZX_PAGE_SIZE);
    EXPECT_EQ(size, ZX_PAGE_SIZE);
  }
  EXPECT_EQ(count, 1);
}

TEST(PhyIterTests, ContiguousTest) {
  constexpr zx_paddr_t kPhysList[] = {
      0 * ZX_PAGE_SIZE,
      1 * ZX_PAGE_SIZE,
      2 * ZX_PAGE_SIZE,
      3 * ZX_PAGE_SIZE,
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = 4 * ZX_PAGE_SIZE,
      .vmo_offset = 0,
      .sg_list = nullptr,
      .sg_count = 0,
  };

  ddk::PhysIter phys_iter(kIterBuffer, 0);
  auto iter = phys_iter.begin();
  const auto end = phys_iter.end();

  EXPECT_TRUE(iter != end);
  auto [paddr, size] = *iter;
  EXPECT_EQ(paddr, 0);
  EXPECT_EQ(size, 4 * ZX_PAGE_SIZE);

  ++iter;
  EXPECT_TRUE(iter == end);
}

TEST(PhyIterTests, DiscontiguousTest) {
  constexpr zx_paddr_t kPhysList[] = {
      1 * ZX_PAGE_SIZE,
      3 * ZX_PAGE_SIZE,
      4 * ZX_PAGE_SIZE,
      7 * ZX_PAGE_SIZE,
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = 4 * ZX_PAGE_SIZE,
      .vmo_offset = 0,
      .sg_list = nullptr,
      .sg_count = 0,
  };

  ddk::PhysIter phys_iter(kIterBuffer, 0);
  auto iter = phys_iter.begin();
  const auto end = phys_iter.end();

  {
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, ZX_PAGE_SIZE);
    EXPECT_EQ(size, ZX_PAGE_SIZE);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 3 * ZX_PAGE_SIZE);
    EXPECT_EQ(size, 2 * ZX_PAGE_SIZE);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 7 * ZX_PAGE_SIZE);
    EXPECT_EQ(size, ZX_PAGE_SIZE);
  }

  ++iter;
  EXPECT_TRUE(iter == end);
}

TEST(PhyIterTests, UnalignedTest) {
  constexpr zx_paddr_t kPhysList[] = {
      2 * ZX_PAGE_SIZE,
      4 * ZX_PAGE_SIZE,
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = 2 * ZX_PAGE_SIZE - 7,
      .vmo_offset = 7,
      .sg_list = nullptr,
      .sg_count = 0,
  };

  ddk::PhysIter phys_iter(kIterBuffer, 0);
  auto iter = phys_iter.begin();
  const auto end = phys_iter.end();

  {
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 2 * ZX_PAGE_SIZE + 7);
    EXPECT_EQ(size, ZX_PAGE_SIZE - 7);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 4 * ZX_PAGE_SIZE);
    EXPECT_EQ(size, ZX_PAGE_SIZE);
  }

  ++iter;
  EXPECT_TRUE(iter == end);
}

TEST(PhyIterTests, ScatterGatherTest) {
  constexpr zx_paddr_t kPhysList[] = {
      1 * ZX_PAGE_SIZE,
      3 * ZX_PAGE_SIZE,
      4 * ZX_PAGE_SIZE,
      7 * ZX_PAGE_SIZE,
  };

  constexpr phys_iter_sg_entry_t kScatterGatherList[] = {
      {10, 1024},
      // Cross contiguous pages.
      {2 * ZX_PAGE_SIZE, ZX_PAGE_SIZE},
      // Cross contiguous pages with offset and non-page size.
      {ZX_PAGE_SIZE + 10, ZX_PAGE_SIZE + 10},
      // Cross nont-contiguous pages and overflow over end.
      {2 * ZX_PAGE_SIZE, 2 * ZX_PAGE_SIZE + 15},
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = 0,
      .vmo_offset = 0,
      .sg_list = kScatterGatherList,
      .sg_count = std::size(kScatterGatherList),
  };

  ddk::PhysIter phys_iter(kIterBuffer, 0);
  auto iter = phys_iter.begin();
  const auto end = phys_iter.end();

  {
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, ZX_PAGE_SIZE + 1024);
    EXPECT_EQ(size, 10);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 3 * ZX_PAGE_SIZE);
    EXPECT_EQ(size, 2 * ZX_PAGE_SIZE);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 3 * ZX_PAGE_SIZE + 10);
    EXPECT_EQ(size, ZX_PAGE_SIZE + 10);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 4 * ZX_PAGE_SIZE + 15);
    EXPECT_EQ(size, ZX_PAGE_SIZE - 15);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 7 * ZX_PAGE_SIZE);
    EXPECT_EQ(size, ZX_PAGE_SIZE);
  }

  ++iter;
  EXPECT_TRUE(iter == end);
}

}  // namespace
