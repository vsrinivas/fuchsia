// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ddk/phys-iter.h"

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
  const zx_paddr_t kPhysList[] = {
      2 * zx_system_get_page_size(),
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = zx_system_get_page_size(),
      .vmo_offset = 0,
      .sg_list = nullptr,
      .sg_count = 0,
  };

  ddk::PhysIter phys_iter(kIterBuffer, 0);
  auto iter = phys_iter.begin();
  const auto end = phys_iter.end();

  EXPECT_TRUE(iter != end);
  auto [paddr, size] = *iter;
  EXPECT_EQ(paddr, 2 * zx_system_get_page_size());
  EXPECT_EQ(size, zx_system_get_page_size());

  ++iter;
  EXPECT_TRUE(iter == end);

  size_t count = 0;
  for (auto [paddr, size] : phys_iter) {
    ++count;
    EXPECT_EQ(paddr, 2 * zx_system_get_page_size());
    EXPECT_EQ(size, zx_system_get_page_size());
  }
  EXPECT_EQ(count, 1);
}

TEST(PhyIterTests, ContiguousTest) {
  const zx_paddr_t kPhysList[] = {
      0 * zx_system_get_page_size(),
      1 * zx_system_get_page_size(),
      2 * zx_system_get_page_size(),
      3 * zx_system_get_page_size(),
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = 4 * zx_system_get_page_size(),
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
  EXPECT_EQ(size, 4 * zx_system_get_page_size());

  ++iter;
  EXPECT_TRUE(iter == end);
}

TEST(PhyIterTests, DiscontiguousTest) {
  const zx_paddr_t kPhysList[] = {
      1 * zx_system_get_page_size(),
      3 * zx_system_get_page_size(),
      4 * zx_system_get_page_size(),
      7 * zx_system_get_page_size(),
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = 4 * zx_system_get_page_size(),
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
    EXPECT_EQ(paddr, zx_system_get_page_size());
    EXPECT_EQ(size, zx_system_get_page_size());
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 3 * zx_system_get_page_size());
    EXPECT_EQ(size, 2 * zx_system_get_page_size());
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 7 * zx_system_get_page_size());
    EXPECT_EQ(size, zx_system_get_page_size());
  }

  ++iter;
  EXPECT_TRUE(iter == end);
}

TEST(PhyIterTests, UnalignedTest) {
  const zx_paddr_t kPhysList[] = {
      2 * zx_system_get_page_size(),
      4 * zx_system_get_page_size(),
  };

  const phys_iter_buffer_t kIterBuffer = {
      .phys = kPhysList,
      .phys_count = std::size(kPhysList),
      .length = 2 * zx_system_get_page_size() - 7,
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
    EXPECT_EQ(paddr, 2 * zx_system_get_page_size() + 7);
    EXPECT_EQ(size, zx_system_get_page_size() - 7);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 4 * zx_system_get_page_size());
    EXPECT_EQ(size, zx_system_get_page_size());
  }

  ++iter;
  EXPECT_TRUE(iter == end);
}

TEST(PhyIterTests, ScatterGatherTest) {
  const zx_paddr_t kPhysList[] = {
      1 * zx_system_get_page_size(),
      3 * zx_system_get_page_size(),
      4 * zx_system_get_page_size(),
      7 * zx_system_get_page_size(),
  };

  const phys_iter_sg_entry_t kScatterGatherList[] = {
      {10, 1024},
      // Cross contiguous pages.
      {2 * zx_system_get_page_size(), zx_system_get_page_size()},
      // Cross contiguous pages with offset and non-page size.
      {zx_system_get_page_size() + 10, zx_system_get_page_size() + 10},
      // Cross nont-contiguous pages and overflow over end.
      {2 * zx_system_get_page_size(), 2 * zx_system_get_page_size() + 15},
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
    EXPECT_EQ(paddr, zx_system_get_page_size() + 1024);
    EXPECT_EQ(size, 10);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 3 * zx_system_get_page_size());
    EXPECT_EQ(size, 2 * zx_system_get_page_size());
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 3 * zx_system_get_page_size() + 10);
    EXPECT_EQ(size, zx_system_get_page_size() + 10);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 4 * zx_system_get_page_size() + 15);
    EXPECT_EQ(size, zx_system_get_page_size() - 15);
  }

  {
    ++iter;
    EXPECT_TRUE(iter != end);
    auto [paddr, size] = *iter;
    EXPECT_EQ(paddr, 7 * zx_system_get_page_size());
    EXPECT_EQ(size, zx_system_get_page_size());
  }

  ++iter;
  EXPECT_TRUE(iter == end);
}

}  // namespace
