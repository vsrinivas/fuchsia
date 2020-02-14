// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical-to-physical-map.h"

#include <zircon/types.h>

#include <utility>

#include <fbl/array.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace nand {

void CheckMultiple(LogicalToPhysicalMap ltop_map, fbl::Vector<fbl::Vector<uint32_t>> expected) {
  for (uint32_t copy = 0; copy < expected.size(); copy++) {
    EXPECT_EQ(ltop_map.AvailableBlockCount(copy), expected[copy].size());
    uint32_t i;
    for (i = 0; i < expected[copy].size(); i++) {
      uint32_t physical;
      zx_status_t status = ltop_map.GetPhysical(copy, i, &physical);
      ASSERT_OK(status);
      EXPECT_EQ(physical, expected[copy][i]);
    }
    uint32_t physical;
    zx_status_t status = ltop_map.GetPhysical(copy, i, &physical);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE);
  }
}

void Check(LogicalToPhysicalMap ltop_map, fbl::Vector<uint32_t> expected) {
  fbl::Vector<fbl::Vector<uint32_t>> expected_;
  expected_.push_back(std::move(expected));
  CheckMultiple(std::move(ltop_map), std::move(expected_));
}

TEST(LogicalToPhysicalMap, SimpleTest) {
  LogicalToPhysicalMap ltop_map(1, 5, fbl::Array<uint32_t>());
  Check(std::move(ltop_map), {0, 1, 2, 3, 4});
}

TEST(LogicalToPhysicalMap, SingleBadTest) {
  fbl::Array<uint32_t> bad_blocks(new uint32_t[1], 1);
  bad_blocks[0] = 2;
  LogicalToPhysicalMap ltop_map(1, 5, std::move(bad_blocks));
  Check(std::move(ltop_map), {0, 1, 3, 4});
}

TEST(LogicalToPhysicalMap, FirstBadTest) {
  fbl::Array<uint32_t> bad_blocks(new uint32_t[1], 1);
  bad_blocks[0] = 0;
  LogicalToPhysicalMap ltop_map(1, 5, std::move(bad_blocks));
  Check(std::move(ltop_map), {1, 2, 3, 4});
}

TEST(LogicalToPhysicalMap, LastBadTest) {
  fbl::Array<uint32_t> bad_blocks(new uint32_t[1], 1);
  bad_blocks[0] = 4;
  LogicalToPhysicalMap ltop_map(1, 5, std::move(bad_blocks));
  Check(std::move(ltop_map), {0, 1, 2, 3});
}

TEST(LogicalToPhysicalMap, MultipleBadTest) {
  fbl::Array<uint32_t> bad_blocks(new uint32_t[3], 3);
  bad_blocks[0] = 0;
  bad_blocks[1] = 2;
  bad_blocks[2] = 4;
  LogicalToPhysicalMap ltop_map(1, 5, std::move(bad_blocks));
  Check(std::move(ltop_map), {1, 3});
}

TEST(LogicalToPhysicalMap, AllBadTest) {
  fbl::Array<uint32_t> bad_blocks(new uint32_t[3], 3);
  bad_blocks[0] = 0;
  bad_blocks[1] = 1;
  bad_blocks[2] = 2;
  LogicalToPhysicalMap ltop_map(1, 3, std::move(bad_blocks));

  EXPECT_EQ(ltop_map.AvailableBlockCount(0), 0);
  uint32_t physical;
  zx_status_t status = ltop_map.GetPhysical(0, 0, &physical);
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE);
}

TEST(LogicalToPhysicalMap, MultipleCopiesTest) {
  LogicalToPhysicalMap ltop_map(4, 8, fbl::Array<uint32_t>());
  fbl::Vector<fbl::Vector<uint32_t>> expected;
  expected.push_back({0, 1});
  expected.push_back({2, 3});
  expected.push_back({4, 5});
  expected.push_back({6, 7});
  CheckMultiple(std::move(ltop_map), std::move(expected));
}

TEST(LogicalToPhysicalMap, MultipleCopiesSomeBadTest) {
  fbl::Array<uint32_t> bad_blocks(new uint32_t[5], 5);
  bad_blocks[0] = 0;
  bad_blocks[1] = 1;
  bad_blocks[2] = 3;
  bad_blocks[3] = 5;
  bad_blocks[4] = 6;
  LogicalToPhysicalMap ltop_map(2, 8, std::move(bad_blocks));
  fbl::Vector<fbl::Vector<uint32_t>> expected;
  expected.push_back({2});
  expected.push_back({4, 7});
  CheckMultiple(std::move(ltop_map), std::move(expected));
}

}  // namespace nand
