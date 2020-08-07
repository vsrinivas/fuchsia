// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma.h"
#include "magma_vendor_queries.h"
#include "platform_buffer.h"

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

TEST(TestQuery, Sram) {
  magma::TestDeviceBase test_device(MAGMA_VENDOR_ID_VSI);

  uint32_t sram_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK,
            magma_query_returns_buffer2(test_device.device(), kMsdVsiVendorQueryExternalSram,
                                        &sram_buffer));

  auto buffer = magma::PlatformBuffer::Import(sram_buffer);
  ASSERT_TRUE(buffer);

  void* ptr;
  ASSERT_TRUE(buffer->MapCpu(&ptr));

  uint64_t phys_addr = *reinterpret_cast<uint64_t*>(ptr);
  EXPECT_EQ(0xFF000000u, phys_addr);

  EXPECT_TRUE(buffer->UnmapCpu());

  // Can't be requested while we have a handle.
  EXPECT_NE(MAGMA_STATUS_OK,
            magma_query_returns_buffer2(test_device.device(), kMsdVsiVendorQueryExternalSram,
                                        &sram_buffer));

  buffer.reset();

  EXPECT_EQ(MAGMA_STATUS_OK,
            magma_query_returns_buffer2(test_device.device(), kMsdVsiVendorQueryExternalSram,
                                        &sram_buffer));

  buffer = magma::PlatformBuffer::Import(sram_buffer);
  ASSERT_TRUE(buffer);
}
