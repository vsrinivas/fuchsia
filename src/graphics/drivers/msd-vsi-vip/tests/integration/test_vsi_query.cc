// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_vendor_queries.h"
#include "magma_vsi_vip_devices.h"
#include "magma_vsi_vip_types.h"
#include "platform_buffer.h"

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kSystemPageCount = 4;

TEST(TestQuery, AddressSpaceRange) {
  magma::TestDeviceBase test_device(MAGMA_VENDOR_ID_VSI);

  uint64_t value;
  ASSERT_EQ(MAGMA_STATUS_OK, magma_query(test_device.device(), kMsdVsiVendorQueryClientGpuAddrRange,
                                         nullptr, &value));

  uint32_t base = static_cast<uint32_t>(value) * kPageSize;
  EXPECT_EQ(base, 0u);

  uint32_t size = (value >> 32) * kPageSize;
  EXPECT_EQ(size, (1u << 31) - kSystemPageCount * kPageSize);
}

TEST(TestQuery, Sram) {
  magma::TestDeviceBase test_device(MAGMA_VENDOR_ID_VSI);

  uint32_t identity_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK, magma_query(test_device.device(), kMsdVsiVendorQueryChipIdentity,
                                         &identity_buffer, nullptr));

  magma_vsi_vip_chip_identity identity;
  {
    auto buffer = magma::PlatformBuffer::Import(identity_buffer);
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(buffer->Read(&identity, 0, sizeof(identity)));
  }
  if ((identity.chip_model == 0x8000 && identity.customer_id == MAGMA_VSI_VIP_NELSON_CUSTOMER_ID) ||
      (identity.chip_model == 0x9000 && identity.customer_id == MAGMA_VSI_VIP_A5_CUSTOMER_ID)) {
    // Nelson has no AXI SRAM.
    // A5 has 2MB AXI SRAM, but dsp module need use it.
    GTEST_SKIP();
  }

  uint32_t sram_buffer;
  EXPECT_EQ(MAGMA_STATUS_OK, magma_query(test_device.device(), kMsdVsiVendorQueryExternalSram,
                                         &sram_buffer, nullptr));

  auto buffer = magma::PlatformBuffer::Import(sram_buffer);
  ASSERT_TRUE(buffer);

  void* ptr;
  ASSERT_TRUE(buffer->MapCpu(&ptr));

  uint64_t phys_addr = *reinterpret_cast<uint64_t*>(ptr);
  EXPECT_EQ(0xFF000000u, phys_addr);

  EXPECT_TRUE(buffer->UnmapCpu());

  // Can't be requested while we have a handle.
  EXPECT_NE(MAGMA_STATUS_OK, magma_query(test_device.device(), kMsdVsiVendorQueryExternalSram,
                                         &sram_buffer, nullptr));

  buffer.reset();

  EXPECT_EQ(MAGMA_STATUS_OK, magma_query(test_device.device(), kMsdVsiVendorQueryExternalSram,
                                         &sram_buffer, nullptr));

  buffer = magma::PlatformBuffer::Import(sram_buffer);
  ASSERT_TRUE(buffer);
}
