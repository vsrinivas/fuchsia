// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "platform_bus_mapper.h"
#include "platform_iommu.h"

class TestPlatformIommu {
 public:
  static void Basic(magma::PlatformBusMapper* mapper, magma::PlatformIommu* iommu,
                    uint32_t page_count) {
    auto buffer = magma::PlatformBuffer::Create(page_count * magma::page_size(), "test");
    ASSERT_TRUE(buffer);

    auto bus_mapping = mapper->MapPageRangeBus(buffer.get(), 0, page_count);
    ASSERT_TRUE(bus_mapping);

    uint64_t gpu_addr = 0x10000000;  // arbitrary
    EXPECT_TRUE(iommu->Map(gpu_addr, bus_mapping.get()));
    EXPECT_TRUE(iommu->Unmap(gpu_addr, bus_mapping.get()));
  }
};

TEST(PlatformIommu, Basic) {
  auto platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  auto iommu = magma::PlatformIommu::Create(platform_device->GetIommuConnector());
  if (!iommu) {
    // Assume unsupported.
    GTEST_SKIP();
  }

  auto mapper = magma::PlatformBusMapper::Create(platform_device->GetBusTransactionInitiator());
  ASSERT_TRUE(mapper);

  TestPlatformIommu::Basic(mapper.get(), iommu.get(), 1);
  TestPlatformIommu::Basic(mapper.get(), iommu.get(), 5);
  TestPlatformIommu::Basic(mapper.get(), iommu.get(), 10);
}
