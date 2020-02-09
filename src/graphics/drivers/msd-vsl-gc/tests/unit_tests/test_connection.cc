// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"
#include "src/graphics/drivers/msd-vsl-gc/src/msd_vsl_connection.h"

class TestMsdVslConnection : public ::testing::Test, public MsdVslConnection::Owner {
 public:
  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch) override {
    return MAGMA_STATUS_UNIMPLEMENTED;
  }

  void SetUp() override {
    static constexpr uint32_t kAddressSpaceIndex = 1;
    address_space_ = AddressSpace::Create(&mock_address_space_owner_, kAddressSpaceIndex);
    EXPECT_NE(address_space_, nullptr);

    connection_ = std::make_shared<MsdVslConnection>(this, address_space_, 0 /* client_id */);
    EXPECT_NE(connection_, nullptr);
  }

 protected:
  class MockAddressSpaceOwner : public AddressSpace::Owner {
   public:
    MockAddressSpaceOwner() : bus_mapper_((1ul << (20 - 1))) {}

    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

    void AddressSpaceReleased(AddressSpace* address_space) override {}

   private:
    MockBusMapper bus_mapper_;
  };

  std::shared_ptr<MsdVslConnection> connection_;

  MockAddressSpaceOwner mock_address_space_owner_;
  std::shared_ptr<AddressSpace> address_space_;
};

TEST_F(TestMsdVslConnection, MapBufferGpu) {
  constexpr uint64_t kBufferSizeInPages = 1;
  constexpr uint64_t kGpuAddr = 0x10000;

  std::shared_ptr<MsdVslBuffer> buffer =
      MsdVslBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");
  EXPECT_EQ(MAGMA_STATUS_OK,
            connection_->MapBufferGpu(buffer, kGpuAddr, 0, kBufferSizeInPages).get());

  std::shared_ptr<GpuMapping> mapping = address_space_->FindGpuMapping(kGpuAddr);
  ASSERT_NE(mapping, nullptr);
  EXPECT_EQ(mapping->BufferId(), buffer->platform_buffer()->id());
}
