// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma.h"
#include "magma_vendor_queries.h"

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kSystemPageCount = 4;

TEST(TestQuery, AddressSpaceRange) {
  magma::TestDeviceBase test_device(MAGMA_VENDOR_ID_VSI);

  uint64_t value;
  ASSERT_EQ(MAGMA_STATUS_OK,
            magma_query2(test_device.device(), kMsdVsiVendorQueryClientGpuAddrRange, &value));

  uint32_t base = static_cast<uint32_t>(value) * kPageSize;
  EXPECT_EQ(base, 0u);

  uint32_t size = (value >> 32) * kPageSize;
  EXPECT_EQ(size, (1u << 31) - kSystemPageCount * kPageSize);
}
