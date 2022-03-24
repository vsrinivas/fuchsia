// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/dev_mem.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using ::testing::ElementsAreArray;
using ::testing::Pair;

constexpr zx_gpaddr_t kGoodDeviceAddr = 0xc000000;

TEST(DevMemTest, NoOverlappingRanges) {
  DevMem dev_mem;

  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr, 0x2000));

  EXPECT_FALSE(dev_mem.AddRange(kGoodDeviceAddr, 0x2000));

  EXPECT_FALSE(dev_mem.AddRange(kGoodDeviceAddr - 0x1000, 0x2000));
  EXPECT_FALSE(dev_mem.AddRange(kGoodDeviceAddr + 0x1000, 0x2000));

  EXPECT_FALSE(dev_mem.AddRange(kGoodDeviceAddr - 1, 2));
  EXPECT_FALSE(dev_mem.AddRange(kGoodDeviceAddr + 0x1fff, 2));
}

TEST(DevMemTest, CanHaveAdjacentRanges) {
  DevMem dev_mem;

  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr, 0x2000));

  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr - 1, 1));
  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr + 0x2000, 1));
}

TEST(DevMemTest, SizedRanges) {
  DevMem dev_mem;

  EXPECT_FALSE(dev_mem.AddRange(kGoodDeviceAddr, 0));
  EXPECT_FALSE(dev_mem.AddRange(0, 0));
}

TEST(DevMemTest, YieldInverseRange) {
  DevMem dev_mem;

  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr, 0x1000));
  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr + 0x4000, 0x1000));

  std::vector<std::pair<zx_gpaddr_t, size_t>> yielded_memory;
  auto yield = [&yielded_memory](zx_gpaddr_t addr_in, size_t size_in) {
    yielded_memory.emplace_back(addr_in, size_in);
  };

  // Memory is non-overlapping (starting at the end of the furthest device memory range).
  dev_mem.YieldInverseRange(kGoodDeviceAddr + 0x5000, 0x1000, yield);
  EXPECT_THAT(yielded_memory, ElementsAreArray({Pair(kGoodDeviceAddr + 0x5000, 0x1000)}));

  // Memory is non-overlapping (ending before the start of the earliest device memory range).
  yielded_memory.clear();
  dev_mem.YieldInverseRange(kGoodDeviceAddr - 0x1000, 0x1000, yield);
  EXPECT_THAT(yielded_memory, ElementsAreArray({Pair(kGoodDeviceAddr - 0x1000, 0x1000)}));

  // Both ends overlap, giving only an internal range.
  yielded_memory.clear();
  dev_mem.YieldInverseRange(kGoodDeviceAddr, 0x5000, yield);
  EXPECT_THAT(yielded_memory, ElementsAreArray({Pair(kGoodDeviceAddr + 0x1000, 0x3000)}));

  // Memory spans all device memory, giving ranges on each side and a range in between.
  yielded_memory.clear();
  dev_mem.YieldInverseRange(kGoodDeviceAddr - 0x1000, 0x7000, yield);
  EXPECT_THAT(yielded_memory, ElementsAreArray({Pair(kGoodDeviceAddr - 0x1000, 0x1000),
                                                Pair(kGoodDeviceAddr + 0x1000, 0x3000),
                                                Pair(kGoodDeviceAddr + 0x5000, 0x1000)}));
}

TEST(DevMemTest, FinalizeRanges) {
  DevMem dev_mem;
  dev_mem.Finalize();

  EXPECT_FALSE(dev_mem.AddRange(kGoodDeviceAddr, 0x1000));
}

TEST(DevMemTest, NoGuestMemoryOverlap) {
  DevMem dev_mem;

  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr, 0x1000));
  EXPECT_FALSE(dev_mem.HasGuestMemoryOverlap(
      {{0, kGoodDeviceAddr}, {kGoodDeviceAddr + 0x1000, kGoodDeviceAddr + 0x2000}}));
}

TEST(DevMemTest, GuestMemoryOverlap) {
  DevMem dev_mem;

  EXPECT_TRUE(dev_mem.AddRange(kGoodDeviceAddr, 0x1000));
  EXPECT_TRUE(dev_mem.HasGuestMemoryOverlap({{0, kGoodDeviceAddr + 0x1000}}));
}

}  // namespace
