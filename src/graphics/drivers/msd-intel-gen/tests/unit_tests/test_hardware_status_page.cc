// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <magma_util/address_space.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

#include "gpu_mapping.h"
#include "hardware_status_page.h"

class TestHardwareStatusPage {
 public:
  using FakeAddressSpace = FakeAllocatingAddressSpace<GpuMapping, magma::AddressSpace<GpuMapping>>;

  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    virtual ~AddressSpaceOwner() = default;
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  void ReadWrite() {
    auto owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<FakeAddressSpace>(owner.get(), 0, UINT32_MAX);

    auto gpu_mapping = FakeAddressSpace::MapBufferGpu(
        address_space, MsdIntelBuffer::Create(magma::page_size(), "hwsp"));
    ASSERT_TRUE(gpu_mapping);

    auto status_page =
        std::make_unique<GlobalHardwareStatusPage>(RENDER_COMMAND_STREAMER, std::move(gpu_mapping));

    uint32_t val = 0xabcd1234;
    status_page->write_sequence_number(val);
    EXPECT_EQ(status_page->read_sequence_number(), val);

    status_page->write_sequence_number(val + 1);
    EXPECT_EQ(status_page->read_sequence_number(), val + 1);
  }
};

TEST(HardwareStatusPage, ReadWrite) {
  TestHardwareStatusPage test;
  test.ReadWrite();
}
