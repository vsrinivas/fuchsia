// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include <gtest/gtest.h>

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

    // +2 so we force multiple notification messages
    for (uint32_t i = 0; i < MSD_CHANNEL_SEND_MAX_SIZE / sizeof(uint64_t) + 2; i++) {
      test_buffer_ids_.push_back(i);
    }
    connection->SendNotification(test_buffer_ids_);
  }

  static void NotificationCallbackStatic(void* token, msd_notification_t* notification) {
    reinterpret_cast<TestMsdIntelConnection*>(token)->NotificationCallback(notification);
  }
  void NotificationCallback(msd_notification_t* notification) {
    EXPECT_EQ(MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND, notification->type);
    constexpr uint32_t kMaxUint64PerSend = MSD_CHANNEL_SEND_MAX_SIZE / sizeof(uint64_t);
    switch (callback_count_++) {
      case 0:
        EXPECT_EQ(kMaxUint64PerSend, notification->u.channel_send.size / sizeof(uint64_t));
        for (uint32_t i = 0; i < notification->u.channel_send.size / sizeof(uint64_t); i++) {
          EXPECT_EQ(test_buffer_ids_[i],
                    reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[i]);
        }
        break;
      case 1:
        EXPECT_EQ(test_buffer_ids_.size() - kMaxUint64PerSend,
                  notification->u.channel_send.size / sizeof(uint64_t));
        for (uint32_t i = 0; i < notification->u.channel_send.size / sizeof(uint64_t); i++) {
          EXPECT_EQ(test_buffer_ids_[kMaxUint64PerSend + i],
                    reinterpret_cast<uint64_t*>(notification->u.channel_send.data)[i]);
        }
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

    constexpr uint64_t kGpuAddr = 0x10000;
    EXPECT_TRUE(AddressSpace::MapBufferGpu(connection->per_process_gtt(), buffer, kGpuAddr, 0, 1,
                                           &mapping));
    ASSERT_TRUE(mapping);
    EXPECT_TRUE(connection->per_process_gtt()->AddMapping(mapping));

    // Release the mapping during the retry.
    auto sleep_callback = [&](uint32_t ms) { mapping.reset(); };

    connection->ReleaseBuffer(buffer->platform_buffer(), sleep_callback);

    EXPECT_EQ(0u, callback_count_);
    EXPECT_FALSE(connection->sent_context_killed());

    const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& mappings =
        connection->mappings_to_release();
    EXPECT_EQ(1u, mappings.size());
  }

  void ReleaseBufferWhileMappedContextKilled() {
    auto connection = MsdIntelConnection::Create(this, 0);
    ASSERT_TRUE(connection);

    connection->SetNotificationCallback(KillCallbackStatic, this);

    std::shared_ptr<MsdIntelBuffer> buffer = MsdIntelBuffer::Create(PAGE_SIZE, "test");
    std::shared_ptr<GpuMapping> mapping;
    EXPECT_TRUE(
        AddressSpace::MapBufferGpu(connection->per_process_gtt(), buffer, 0x10000, 0, 1, &mapping));
    ASSERT_TRUE(mapping);
    EXPECT_TRUE(connection->per_process_gtt()->AddMapping(mapping));

    // Release the buffer while holding the mapping retries for a while then
    // triggers the killed callback.
    uint32_t sleep_callback_count = 0;
    auto sleep_callback = [&](uint32_t ms) { sleep_callback_count += 1; };

    connection->ReleaseBuffer(buffer->platform_buffer(), sleep_callback);

    EXPECT_GT(sleep_callback_count, 0u);
    EXPECT_EQ(1u, callback_count_);
    EXPECT_TRUE(connection->sent_context_killed());

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
  std::vector<uint64_t> test_buffer_ids_;
  uint32_t callback_count_ = 0;
};

TEST_F(TestMsdIntelConnection, Notification) { Notification(); }

TEST_F(TestMsdIntelConnection, ReleaseBuffer) { ReleaseBuffer(); }

TEST_F(TestMsdIntelConnection, ReleaseBufferWhileMapped) { ReleaseBufferWhileMapped(); }

TEST_F(TestMsdIntelConnection, ReleaseBufferWhileMappedContextKilled) {
  ReleaseBufferWhileMappedContextKilled();
}

TEST_F(TestMsdIntelConnection, ReuseGpuAddrWithoutRelease) { ReuseGpuAddrWithoutRelease(); }

TEST_F(TestMsdIntelConnection, InheritanceCheck) {
  EXPECT_FALSE(static_cast<bool>(std::is_base_of<PerProcessGtt::Owner, MsdIntelConnection>()));
}
