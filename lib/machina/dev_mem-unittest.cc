// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/dev_mem.h"

#include "gtest/gtest.h"

namespace machina {
namespace {

static constexpr zx_gpaddr_t kGoodDeviceAddr =
    DevMem::kAddrLowerBound + 0x10000;

TEST(DevMemTest, GoodRanges) {
  EXPECT_TRUE(DevMem().AddRange(kGoodDeviceAddr, 0x2000));
  EXPECT_TRUE(
      DevMem().AddRange(DevMem::kAddrLowerBound,
                        DevMem::kAddrUpperBound - DevMem::kAddrLowerBound));
}

TEST(DevMemTest, BadRanges) {
  DevMem dev_mem;
  EXPECT_FALSE(dev_mem.AddRange(0, 0x1000));
  EXPECT_FALSE(dev_mem.AddRange(DevMem::kAddrLowerBound - 1, 1));
  EXPECT_FALSE(dev_mem.AddRange(DevMem::kAddrUpperBound, 0x1000));

  EXPECT_FALSE(dev_mem.AddRange(DevMem::kAddrLowerBound - 1, 2));
  EXPECT_FALSE(dev_mem.AddRange(DevMem::kAddrLowerBound - 1, 2));
  EXPECT_FALSE(
      dev_mem.AddRange(DevMem::kAddrUpperBound - 1,
                       DevMem::kAddrUpperBound - DevMem::kAddrLowerBound + 2));
}

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
  EXPECT_FALSE(dev_mem.AddRange(DevMem::kAddrLowerBound - 1, 0));
  EXPECT_FALSE(dev_mem.AddRange(DevMem::kAddrUpperBound, 0));
}

}  // namespace
}  // namespace machina
