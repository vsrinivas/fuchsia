// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "mock/mock_bus_mapper.h"
#include "src/graphics/drivers/msd-vsi-vip/src/address_space_layout.h"
#include "src/graphics/drivers/msd-vsi-vip/src/mapped_batch.h"
#include "src/graphics/drivers/msd-vsi-vip/src/msd_vsi_connection.h"

class TestMsdVsiConnection : public ::testing::Test, public MsdVsiConnection::Owner {
 public:
  Ringbuffer* GetRingbuffer() override { return nullptr; }

  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch, bool do_flush) override {
    return MAGMA_STATUS_UNIMPLEMENTED;
  }

  void SetUp() override {
    static constexpr uint32_t kAddressSpaceIndex = 1;
    address_space_ = AddressSpace::Create(&mock_address_space_owner_, kAddressSpaceIndex);
    EXPECT_NE(address_space_, nullptr);

    connection_ = std::make_shared<MsdVsiConnection>(this, address_space_, 0 /* client_id */);
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

  static void KillCallbackStatic(void* token, msd_notification_t* notification) {
    EXPECT_EQ(MSD_CONNECTION_NOTIFICATION_CONTEXT_KILLED, notification->type);
    reinterpret_cast<TestMsdVsiConnection*>(token)->callback_count_++;
  }

  std::shared_ptr<MsdVsiConnection> connection_;

  MockAddressSpaceOwner mock_address_space_owner_;
  std::shared_ptr<AddressSpace> address_space_;

  uint32_t callback_count_ = 0;
};

TEST_F(TestMsdVsiConnection, MapBufferGpu) {
  constexpr uint64_t kBufferSizeInPages = 1;
  constexpr uint64_t kGpuAddr = 0x10000;

  std::shared_ptr<MsdVsiBuffer> buffer =
      MsdVsiBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");
  EXPECT_EQ(MAGMA_STATUS_OK,
            connection_->MapBufferGpu(buffer, kGpuAddr, 0, kBufferSizeInPages).get());

  std::shared_ptr<GpuMapping> mapping = address_space_->FindGpuMapping(kGpuAddr);
  ASSERT_NE(mapping, nullptr);
  EXPECT_EQ(mapping->BufferId(), buffer->platform_buffer()->id());
}

TEST_F(TestMsdVsiConnection, ReleaseMapping) {
  constexpr uint64_t kBufferSizeInPages = 2;
  constexpr uint64_t kGpuAddr1 = 0x10000;
  constexpr uint64_t kGpuAddr2 = 0x20000;

  connection_->SetNotificationCallback(KillCallbackStatic, this);

  // Add separate mappings for the buffer's pages.

  // Add the first mapping.
  std::shared_ptr<MsdVsiBuffer> buffer =
      MsdVsiBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");
  std::shared_ptr<GpuMapping> mapping1;
  EXPECT_TRUE(AddressSpace::MapBufferGpu(address_space_, buffer, kGpuAddr1, 0, 1 /* page_count */,
                                         &mapping1));
  ASSERT_TRUE(mapping1);
  EXPECT_TRUE(address_space_->AddMapping(mapping1));

  // Add the second mapping.
  std::shared_ptr<GpuMapping> mapping2;
  EXPECT_TRUE(AddressSpace::MapBufferGpu(address_space_, buffer, kGpuAddr2, 0, 1 /* page_count */,
                                         &mapping2));
  ASSERT_TRUE(mapping2);
  EXPECT_TRUE(address_space_->AddMapping(mapping2));

  // Release our reference to the first mapping before calling |ReleaseMapping|.
  mapping1.reset();
  EXPECT_TRUE(connection_->ReleaseMapping(buffer->platform_buffer(), kGpuAddr1));
  EXPECT_EQ(0u, callback_count_);

  const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& mappings1 =
      connection_->mappings_to_release();
  EXPECT_EQ(1u, mappings1.size());

  // Calling |ReleaseMapping| while holding a reference to the mapping triggers the killed callback
  EXPECT_TRUE(connection_->ReleaseMapping(buffer->platform_buffer(), kGpuAddr2));
  EXPECT_EQ(1u, callback_count_);

  const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& mappings2 =
      connection_->mappings_to_release();
  // Still holds the first mapping.
  EXPECT_EQ(1u, mappings2.size());
}

TEST_F(TestMsdVsiConnection, ReleaseBuffer) {
  constexpr uint64_t kBufferSizeInPages = 1;
  constexpr uint64_t kGpuAddr = 0x10000;

  connection_->SetNotificationCallback(KillCallbackStatic, this);

  std::shared_ptr<MsdVsiBuffer> buffer =
      MsdVsiBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");
  std::shared_ptr<GpuMapping> mapping;
  EXPECT_TRUE(AddressSpace::MapBufferGpu(address_space_, buffer, kGpuAddr, 0, kBufferSizeInPages,
                                         &mapping));
  ASSERT_TRUE(mapping);
  EXPECT_TRUE(address_space_->AddMapping(mapping));

  mapping.reset();

  connection_->ReleaseBuffer(buffer->platform_buffer());
  EXPECT_EQ(0u, callback_count_);

  const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& mappings =
      connection_->mappings_to_release();
  EXPECT_EQ(1u, mappings.size());
}

TEST_F(TestMsdVsiConnection, ReleaseBufferWhileMapped) {
  constexpr uint64_t kBufferSizeInPages = 1;
  constexpr uint64_t kGpuAddr = 0x10000;

  connection_->SetNotificationCallback(KillCallbackStatic, this);

  std::shared_ptr<MsdVsiBuffer> buffer =
      MsdVsiBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");
  std::shared_ptr<GpuMapping> mapping;
  EXPECT_TRUE(AddressSpace::MapBufferGpu(address_space_, buffer, kGpuAddr, 0, kBufferSizeInPages,
                                         &mapping));
  ASSERT_TRUE(mapping);
  EXPECT_TRUE(address_space_->AddMapping(mapping));

  // Releasing the buffer while holding the mapping triggers the killed callback
  connection_->ReleaseBuffer(buffer->platform_buffer());
  EXPECT_EQ(1u, callback_count_);

  const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& mappings =
      connection_->mappings_to_release();
  EXPECT_EQ(0u, mappings.size());
}

TEST_F(TestMsdVsiConnection, AddressSpaceDirty) {
  constexpr uint64_t kBufferSizeInPages = 1;
  constexpr uint64_t kGpuAddr = 0x10000;

  EXPECT_FALSE(connection_->address_space_dirty());

  std::shared_ptr<MsdVsiBuffer> buffer =
      MsdVsiBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");
  EXPECT_EQ(MAGMA_STATUS_OK,
            connection_->MapBufferGpu(buffer, kGpuAddr, 0, kBufferSizeInPages).get());

  EXPECT_TRUE(connection_->address_space_dirty());

  EXPECT_EQ(MAGMA_STATUS_UNIMPLEMENTED,
            connection_->SubmitBatch(std::make_unique<NullBatch>()).get());

  EXPECT_FALSE(connection_->address_space_dirty());
}

// Tests that the client can map only within the client reserved region.
TEST_F(TestMsdVsiConnection, OnlyMapAllowedGpuAddresses) {
  constexpr uint64_t kBufferSizeInPages = 2;
  std::shared_ptr<MsdVsiBuffer> buffer =
      MsdVsiBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");

  // Test mapping at the beginning of the client reserved region.
  uint32_t gpu_addr = AddressSpaceLayout::client_gpu_addr_base();
  EXPECT_EQ(MAGMA_STATUS_OK,
            connection_->MapBufferGpu(buffer, gpu_addr, 0, kBufferSizeInPages).get());

  // Test mapping at the end of the client reserved region.
  uint32_t client_reserved_gpu_addr_end =
      AddressSpaceLayout::client_gpu_addr_base() + AddressSpaceLayout::client_gpu_addr_size();
  gpu_addr = client_reserved_gpu_addr_end - (kBufferSizeInPages * magma::page_size());
  EXPECT_EQ(MAGMA_STATUS_OK,
            connection_->MapBufferGpu(buffer, gpu_addr, 0, kBufferSizeInPages).get());

  // Test mapping that extends beyond the end of the client reserved region.
  gpu_addr = client_reserved_gpu_addr_end - magma::page_size();
  EXPECT_NE(MAGMA_STATUS_OK,
            connection_->MapBufferGpu(buffer, gpu_addr, 0, kBufferSizeInPages).get());
}
