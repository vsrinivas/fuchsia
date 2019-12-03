// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/address_space.h"
#include "garnet/drivers/gpu/msd-vsl-gc/src/ringbuffer.h"
#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"

class RingbufferTest : public ::testing::Test {
 public:
  class MockAddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    // Put bus addresses close to the 40 bit limit
    MockAddressSpaceOwner() : bus_mapper_((1ul << (40 - 1))) {}

    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  void SetUp() override {
    const uint32_t kRingbufferSize = magma::page_size();
    ringbuffer_ = std::make_unique<Ringbuffer>(
        MsdVslBuffer::Create(kRingbufferSize, "ringbuffer"), 0 /* start_offset */);
    EXPECT_NE(ringbuffer_, nullptr);
  }

  std::unique_ptr<Ringbuffer> ringbuffer_;
};

TEST_F(RingbufferTest, Map) {
  MockAddressSpaceOwner owner;
  std::shared_ptr<AddressSpace> address_space = AddressSpace::Create(&owner);
  ASSERT_NE(nullptr, address_space);

  EXPECT_TRUE(ringbuffer_->Map(address_space));
}
