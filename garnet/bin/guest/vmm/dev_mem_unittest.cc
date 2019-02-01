// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/dev_mem.h"

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

}  // namespace
