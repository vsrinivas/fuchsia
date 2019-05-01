// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/dev_mem.h"

#include "gtest/gtest.h"

namespace {

static constexpr zx_gpaddr_t kGoodDeviceAddr = 0xc000000;

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

  zx_gpaddr_t addr = 0;
  size_t size = 0;
  auto yield = [&addr, &size](zx_gpaddr_t addr_in, size_t size_in) {
    addr = addr_in;
    size = size_in;
  };

  // Memory is non-overlapping.
  dev_mem.YieldInverseRange(kGoodDeviceAddr + 0x1000, 0x1000, yield);
  EXPECT_EQ(kGoodDeviceAddr + 0x1000, addr);
  EXPECT_EQ(0x1000ul, size);

  // Memory extends beyond end of range.
  addr = 0;
  size = 0;
  dev_mem.YieldInverseRange(kGoodDeviceAddr, 0x3000, yield);
  EXPECT_EQ(kGoodDeviceAddr + 0x1000, addr);
  EXPECT_EQ(0x2000ul, size);

  // Memory begins before start of range.
  addr = 0;
  size = 0;
  dev_mem.YieldInverseRange(0, kGoodDeviceAddr + 0x1000, yield);
  EXPECT_EQ(0ul, addr);
  EXPECT_EQ(kGoodDeviceAddr, size);
}

}  // namespace
