// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <poll.h>

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma.h"
#include "magma_arm_mali_types.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"

namespace {
class TestConnection : public magma::TestDeviceBase {
 public:
  TestConnection() : magma::TestDeviceBase(MAGMA_VENDOR_ID_MALI) {
    EXPECT_EQ(MAGMA_STATUS_OK, magma_create_connection2(device(), &connection_));
    DASSERT(connection_);

    magma_create_context(connection_, &context_id_);
  }

  ~TestConnection() {
    magma_release_context(connection_, context_id_);

    if (connection_)
      magma_release_connection(connection_);
  }

  bool AccessPerfCounters() {
    for (auto& p : std::filesystem::directory_iterator("/dev/class/gpu-performance-counters")) {
      zx::channel server_end, client_end;
      zx::channel::create(0, &server_end, &client_end);

      zx_status_t zx_status = fdio_service_connect(p.path().c_str(), server_end.release());
      EXPECT_EQ(ZX_OK, zx_status);
      magma_status_t status =
          magma_connection_access_performance_counters(connection_, client_end.release());
      EXPECT_TRUE(status == MAGMA_STATUS_OK || status == MAGMA_STATUS_ACCESS_DENIED);
      if (status == MAGMA_STATUS_OK) {
        return true;
      }
    }
    return false;
  }

  void TestPerfCounters() {
    EXPECT_TRUE(AccessPerfCounters());

    magma_buffer_t buffer;
    uint64_t buffer_size;
    constexpr uint32_t kPerfCountBufferSize = 2048;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_create_buffer(connection_, kPerfCountBufferSize * 2, &buffer_size, &buffer));

    magma_perf_count_pool_t pool;
    magma_handle_t notification_handle;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_connection_create_performance_counter_buffer_pool(
                                   connection_, &pool, &notification_handle));

    uint64_t perf_counter_id = 1;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_connection_enable_performance_counters(connection_, &perf_counter_id, 1));
    magma_buffer_offset offsets[2];
    offsets[0].buffer_id = magma_get_buffer_id(buffer);
    offsets[0].offset = 0;
    offsets[0].length = kPerfCountBufferSize;
    offsets[1].buffer_id = magma_get_buffer_id(buffer);
    offsets[1].offset = kPerfCountBufferSize;
    offsets[1].length = kPerfCountBufferSize;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_connection_add_performance_counter_buffer_offsets_to_pool(
                                   connection_, pool, offsets, 2));

    uint64_t start_time = zx::clock::get_monotonic().get();

    // Trigger three dumps at once. The last one should be dropped.
    constexpr uint32_t kTriggerId = 5;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_connection_dump_performance_counters(connection_, pool, kTriggerId));

    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_connection_dump_performance_counters(connection_, pool, kTriggerId + 1));
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_connection_dump_performance_counters(connection_, pool, kTriggerId + 2));

    for (uint32_t i = 0; i < 2; i++) {
      magma_poll_item_t poll_item{};
      poll_item.type = MAGMA_POLL_TYPE_HANDLE;
      poll_item.condition = MAGMA_POLL_CONDITION_READABLE;
      poll_item.handle = notification_handle;
      EXPECT_EQ(MAGMA_STATUS_OK, magma_poll(&poll_item, 1, INT64_MAX));

      uint64_t last_possible_time = zx::clock::get_monotonic().get();

      uint32_t trigger_id;
      uint64_t buffer_id;
      uint32_t buffer_offset;
      uint64_t time;
      uint32_t result_flags;
      EXPECT_EQ(MAGMA_STATUS_OK, magma_connection_read_performance_counter_completion(
                                     connection_, pool, &trigger_id, &buffer_id, &buffer_offset,
                                     &time, &result_flags));

      EXPECT_EQ(magma_get_buffer_id(buffer), buffer_id);
      EXPECT_TRUE(trigger_id == kTriggerId || trigger_id == kTriggerId + 1);
      bool expected_discontinuous = i == 0;
      uint32_t expected_result_flags =
          expected_discontinuous ? MAGMA_PERF_COUNTER_RESULT_DISCONTINUITY : 0;
      EXPECT_EQ(expected_result_flags, result_flags);
      EXPECT_LE(start_time, time);
      EXPECT_LE(time, last_possible_time);

      void* data;
      magma_map(connection_, buffer, &data);
      auto data_dwords =
          reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(data) + buffer_offset);
      constexpr uint32_t kEnableBitsOffset = 2;
      if (i == 0) {
        EXPECT_EQ(0x80ffu, data_dwords[kEnableBitsOffset]);
      }
    }

    uint32_t trigger_id;
    uint64_t buffer_id;
    uint32_t buffer_offset;
    uint64_t time;
    uint32_t result_flags;
    EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, magma_connection_read_performance_counter_completion(
                                          connection_, pool, &trigger_id, &buffer_id,
                                          &buffer_offset, &time, &result_flags));

    magma_connection_release_performance_counter_buffer_pool(connection_, pool);

    magma_release_buffer(connection_, buffer);
  }

 private:
  magma_connection_t connection_;
  uint32_t context_id_;
};

TEST(PerfCounters, Basic) {
  TestConnection connection;
  connection.TestPerfCounters();
}
}  // namespace
