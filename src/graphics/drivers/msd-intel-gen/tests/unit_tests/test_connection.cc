// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"
#include "msd_intel_connection.h"

class TestMsdIntelConnection : public ::testing::Test, public MsdIntelConnection::Owner {
 public:
  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch) override {
    return MAGMA_STATUS_UNIMPLEMENTED;
  }

  void DestroyContext(std::shared_ptr<ClientContext> client_context) override {}

  magma::PlatformBusMapper* GetBusMapper() override { return &mock_bus_mapper_; }

  void Notification() {
    auto connection = MsdIntelConnection::Create(this, 0);
    ASSERT_TRUE(connection);

    connection->SetNotificationCallback(NotificationCallbackStatic, this);
    connection->SendNotification(test_buffer_ids_);
  }

  static void NotificationCallbackStatic(void* token, msd_notification_t* notification) {
    reinterpret_cast<TestMsdIntelConnection*>(token)->NotificationCallback(notification);
  }
  void NotificationCallback(msd_notification_t* notification) {
    EXPECT_EQ(MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND, notification->type);
    switch (callback_count_++) {
      case 0:
        EXPECT_EQ(64u, notification->u.channel_send.size);
        EXPECT_EQ(test_buffer_ids_[0],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[0]);
        EXPECT_EQ(test_buffer_ids_[1],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[1]);
        EXPECT_EQ(test_buffer_ids_[2],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[2]);
        EXPECT_EQ(test_buffer_ids_[3],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[3]);
        EXPECT_EQ(test_buffer_ids_[4],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[4]);
        EXPECT_EQ(test_buffer_ids_[5],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[5]);
        EXPECT_EQ(test_buffer_ids_[6],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[6]);
        EXPECT_EQ(test_buffer_ids_[7],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[7]);
        break;
      case 1:
        EXPECT_EQ(16u, notification->u.channel_send.size);
        EXPECT_EQ(test_buffer_ids_[8],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[0]);
        EXPECT_EQ(test_buffer_ids_[9],
                  reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[1]);
        break;
      default:
        EXPECT_TRUE(false);
    }
  }

  void ReleaseBuffer() {
    auto connection = MsdIntelConnection::Create(this, 0);
    ASSERT_TRUE(connection);

    connection->SetNotificationCallback(KillCallbackStatic, this);

    std::shared_ptr<MsdIntelBuffer> buffer = MsdIntelBuffer::Create(PAGE_SIZE, "test");
    std::shared_ptr<GpuMapping> mapping;
    EXPECT_TRUE(
        AddressSpace::MapBufferGpu(connection->per_process_gtt(), buffer, 0x10000, 0, 1, &mapping));
    ASSERT_TRUE(mapping);
    EXPECT_TRUE(connection->per_process_gtt()->AddMapping(mapping));

    mapping.reset();
    connection->ReleaseBuffer(buffer->platform_buffer());
    EXPECT_EQ(0u, callback_count_);

    const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& mappings =
        connection->mappings_to_release();
    EXPECT_EQ(1u, mappings.size());
  }

  void ReleaseBufferWhileMapped() {
    auto connection = MsdIntelConnection::Create(this, 0);
    ASSERT_TRUE(connection);

    connection->SetNotificationCallback(KillCallbackStatic, this);

    std::shared_ptr<MsdIntelBuffer> buffer = MsdIntelBuffer::Create(PAGE_SIZE, "test");
    std::shared_ptr<GpuMapping> mapping;
    EXPECT_TRUE(
        AddressSpace::MapBufferGpu(connection->per_process_gtt(), buffer, 0x10000, 0, 1, &mapping));
    ASSERT_TRUE(mapping);
    EXPECT_TRUE(connection->per_process_gtt()->AddMapping(mapping));

    // Release the buffer while holding the mapping triggers the killed callback
    connection->ReleaseBuffer(buffer->platform_buffer());
    EXPECT_EQ(1u, callback_count_);

    const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& mappings =
        connection->mappings_to_release();
    EXPECT_EQ(0u, mappings.size());
  }

  void ReuseGpuAddrWithoutRelease() {
    auto connection = MsdIntelConnection::Create(this, 0);
    ASSERT_TRUE(connection);

    constexpr uint64_t kBufferSizeInPages = 1;
    constexpr uint64_t kGpuAddr = 0x10000;

    for (uint32_t i = 0; i < 2; i++) {
      std::shared_ptr<MsdIntelBuffer> buffer =
          MsdIntelBuffer::Create(kBufferSizeInPages * magma::page_size(), "test");
      EXPECT_EQ(MAGMA_STATUS_OK,
                connection->MapBufferGpu(buffer, kGpuAddr, 0, kBufferSizeInPages).get());

      std::shared_ptr<GpuMapping> mapping = connection->per_process_gtt()->FindGpuMapping(kGpuAddr);
      ASSERT_TRUE(mapping);
      EXPECT_EQ(mapping->BufferId(), buffer->platform_buffer()->id());
    }
  }

  static void KillCallbackStatic(void* token, msd_notification_t* notification) {
    EXPECT_EQ(MSD_CONNECTION_NOTIFICATION_CONTEXT_KILLED, notification->type);
    reinterpret_cast<TestMsdIntelConnection*>(token)->callback_count_++;
  }

 private:
  MockBusMapper mock_bus_mapper_;
  std::vector<uint64_t> test_buffer_ids_{10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
  uint32_t callback_count_ = 0;
};

TEST_F(TestMsdIntelConnection, Notification) { Notification(); }

TEST_F(TestMsdIntelConnection, ReleaseBuffer) { ReleaseBuffer(); }

TEST_F(TestMsdIntelConnection, ReleaseBufferWhileMapped) { ReleaseBufferWhileMapped(); }

TEST_F(TestMsdIntelConnection, ReuseGpuAddrWithoutRelease) { ReuseGpuAddrWithoutRelease(); }

TEST_F(TestMsdIntelConnection, InheritanceCheck) {
  EXPECT_FALSE(static_cast<bool>(std::is_base_of<PerProcessGtt::Owner, MsdIntelConnection>()));
}
