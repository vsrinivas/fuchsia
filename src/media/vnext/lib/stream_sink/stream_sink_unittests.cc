// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/test_loop_fixture.h>

#include "src/media/vnext/lib/stream_sink/clear_request.h"
#include "src/media/vnext/lib/stream_sink/release_fence.h"
#include "src/media/vnext/lib/stream_sink/stream_sink_client.h"
#include "src/media/vnext/lib/stream_sink/stream_sink_impl.h"

namespace fmlib {
namespace test {

class BufferCollection {
 public:
  BufferCollection() = default;

  ~BufferCollection() = default;

  static void* GetMappedPayload(fuchsia::media2::PayloadRange& payload_range) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<void*>(payload_range.offset);
  }
};

class Packet {
 public:
  Packet(BufferCollection* buffer_collection, fuchsia::media2::PayloadRange payload_range,
         int64_t timestamp, std::unique_ptr<ReleaseFence> release_fence = nullptr)
      : payload_range_(payload_range),
        size_(payload_range.size),
        timestamp_(timestamp),
        release_fence_(std::move(release_fence)) {
    EXPECT_TRUE(!!buffer_collection);
    payload_ = BufferCollection::GetMappedPayload(payload_range_);
  }

  Packet(fuchsia::media2::PayloadRange payload_range, void* payload, int64_t timestamp,
         std::unique_ptr<ReleaseFence> release_fence = nullptr)
      : payload_range_(payload_range),
        payload_(payload),
        size_(payload_range.size),
        timestamp_(timestamp),
        release_fence_(std::move(release_fence)) {}

  ~Packet() {
    if (dispose_callback_) {
      dispose_callback_();
    }
  }

  fuchsia::media2::PayloadRange payload_range_;
  void* payload_;
  size_t size_;
  int64_t timestamp_;
  std::unique_ptr<ReleaseFence> release_fence_;
  fit::closure dispose_callback_;
};

}  // namespace test

template <>
struct ToPacketConverter<std::unique_ptr<test::Packet>> {
  static fuchsia::media2::Packet Convert(std::unique_ptr<test::Packet>& t) {
    EXPECT_TRUE(!!t);
    return fuchsia::media2::Packet{
        .payload = {t->payload_range_},
        .timestamp = fuchsia::media2::PacketTimestamp::WithSpecified(t->timestamp_ + 0)};
  }
};

template <>
struct FromPacketConverter<std::unique_ptr<test::Packet>, test::BufferCollection*> {
  static std::unique_ptr<test::Packet> Convert(fuchsia::media2::Packet packet,
                                               std::unique_ptr<ReleaseFence> release_fence,
                                               test::BufferCollection* context) {
    EXPECT_EQ(1u, packet.payload.size());
    EXPECT_TRUE(packet.timestamp.is_specified());
    EXPECT_TRUE(release_fence);
    return std::make_unique<test::Packet>(context, packet.payload[0], packet.timestamp.specified(),
                                          std::move(release_fence));
  }
};

namespace test {

using Queue = StreamQueue<std::unique_ptr<test::Packet>, ClearRequest>;

class StreamSinkTest : public gtest::TestLoopFixture {
 public:
  StreamSinkTest() : executor_(dispatcher()) {
    fuchsia::media2::StreamSinkHandle handle;
    service_under_test_.Connect(handle.NewRequest(), &service_queue_, &buffer_collection_);
    client_under_test_.Connect(executor_, &client_queue_, std::move(handle));
  }

 protected:
  static constexpr uint32_t kBufferId = 1234;

  async::Executor& executor() { return executor_; }
  StreamSinkClient<std::unique_ptr<test::Packet>>& client_under_test() {
    return client_under_test_;
  }
  StreamSinkImpl<std::unique_ptr<test::Packet>, BufferCollection*>& service_under_test() {
    return service_under_test_;
  }
  Queue& client_queue() { return client_queue_; }
  Queue& service_queue() { return service_queue_; }

 private:
  async::Executor executor_;
  BufferCollection buffer_collection_;
  Queue client_queue_;
  Queue service_queue_;
  StreamSinkClient<std::unique_ptr<test::Packet>> client_under_test_;
  StreamSinkImpl<std::unique_ptr<test::Packet>, BufferCollection*> service_under_test_;
};

// Tests that a newly-connected client/service pair has the correct initial state.
TEST_F(StreamSinkTest, Initial) {
  EXPECT_TRUE(client_under_test().is_connected());
  EXPECT_TRUE(!!client_under_test());
  EXPECT_TRUE(service_under_test().is_connected());
  EXPECT_TRUE(!!service_under_test());
}

// Tests that one packet is moved properly from client to service and that the closing of the
// release fence is signaled properly back to the client.
TEST_F(StreamSinkTest, OnePacket) {
  constexpr size_t kSize = 4321;
  constexpr int64_t kTimestamp = 2345;

  auto packet = std::make_unique<Packet>(
      fuchsia::media2::PayloadRange{.buffer_id = 0, .offset = 0, .size = kSize}, nullptr,
      kTimestamp);
  bool packet_disposed = false;
  packet->dispose_callback_ = [&packet_disposed]() { packet_disposed = true; };

  // Push a packet on the client side.
  client_queue().push(std::move(packet));

  // Pull a packet on the service side.
  std::unique_ptr<ReleaseFence> release_fence;
  executor().schedule_task(
      service_queue().pull().then([&release_fence, kSize, kTimestamp](Queue::PullResult& result) {
        EXPECT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().is_packet());
        auto& packet = result.value().packet();
        EXPECT_EQ(nullptr, packet->payload_);
        EXPECT_EQ(kSize, packet->size_);
        EXPECT_EQ(kTimestamp, packet->timestamp_);
        EXPECT_TRUE(!!packet->release_fence_);
        release_fence = std::move(packet->release_fence_);
      }));

  // Expect that nothing has happened yet, because the dispatcher hasn't run.
  EXPECT_FALSE(release_fence);
  EXPECT_FALSE(packet_disposed);

  RunLoopUntilIdle();

  // Expect that the packet has arrived on the service side, and the original hasn't yet been
  // disposed on the client side.
  EXPECT_TRUE(release_fence);
  EXPECT_FALSE(packet_disposed);

  // Delete the release fence.
  release_fence.reset();

  // Expect that the original packet has not been disposed, because the dispatcher hasn't run.
  EXPECT_FALSE(packet_disposed);

  RunLoopUntilIdle();

  // Expect that the original packet has been disposed.
  EXPECT_TRUE(packet_disposed);
}

// Tests that an empty stream (end only, no packets) is moved properly.
TEST_F(StreamSinkTest, EndOnly) {
  // End the stream on the client side.
  client_queue().end();

  // Try to pull a packet on the service side, expect to get end instead.
  bool ended = false;
  executor().schedule_task(service_queue().pull().then([&ended](Queue::PullResult& result) {
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_ended());
    ended = true;
  }));

  // Expect that nothing has happened yet, because the dispatcher hasn't run.
  EXPECT_FALSE(ended);

  RunLoopUntilIdle();

  // Expect that the stream ended on the service side.
  EXPECT_TRUE(ended);
}

// Tests that clear is received properly.
TEST_F(StreamSinkTest, ClearOnly) {
  zx::eventpair completion_fence_client;
  zx::eventpair completion_fence_service;
  zx_status_t status =
      zx::eventpair::create(0, &completion_fence_client, &completion_fence_service);
  EXPECT_EQ(ZX_OK, status);

  // Clear the queue on the client side.
  client_queue().clear(ClearRequest(true, std::move(completion_fence_service)));

  // Try to pull a packet on the service side, expect to get clear instead.
  bool cleared = false;
  executor().schedule_task(service_queue().pull().then([&cleared](Queue::PullResult& result) {
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_clear_request());
    // TODO(dalesat): validate result.value().clear_request()
    cleared = true;
  }));

  // Expect that nothing has happened yet, because the dispatcher hasn't run.
  EXPECT_FALSE(cleared);

  RunLoopUntilIdle();

  // Expect that the queue is cleared on the service side.
  EXPECT_TRUE(cleared);
}

// Tests that disconnection notifications work on the client side.
TEST_F(StreamSinkTest, ClientNormalDisconnect) {
  bool disconnected = false;
  executor().schedule_task(client_under_test().WhenDisconnected().then(
      [&disconnected](fpromise::result<void, zx_status_t>& result) {
        EXPECT_TRUE(result.is_ok());
        disconnected = true;
      }));

  RunLoopUntilIdle();

  // Expect that nothing has happened yet.
  EXPECT_FALSE(disconnected);

  // Disconnect.
  EXPECT_TRUE(!!client_under_test().Disconnect());
  RunLoopUntilIdle();

  // Expect that the disconnect task ran, because |Disconnect| was called.
  EXPECT_TRUE(disconnected);

  disconnected = false;
  executor().schedule_task(client_under_test().WhenDisconnected().then(
      [&disconnected](fpromise::result<void, zx_status_t>& result) {
        EXPECT_TRUE(result.is_ok());
        disconnected = true;
      }));

  RunLoopUntilIdle();

  // Expect that the disconnect task ran, because the client was already disconnected.
  EXPECT_TRUE(disconnected);
}

// Tests that disconnection notifications work on the client side when the channel is closed.
TEST_F(StreamSinkTest, ClientSurpriseDisconnect) {
  bool disconnected = false;
  executor().schedule_task(client_under_test().WhenDisconnected().then(
      [&disconnected](fpromise::result<void, zx_status_t>& result) {
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.error());
        disconnected = true;
      }));

  RunLoopUntilIdle();

  // Expect that nothing has happened yet.
  EXPECT_FALSE(disconnected);

  // Disconnect the service, closing the channel from that end.
  EXPECT_TRUE(!!service_under_test().Disconnect());
  RunLoopUntilIdle();

  // Expect that the disconnect notification task ran.
  EXPECT_TRUE(disconnected);
}

// Tests that disconnection notifications work on the service side.
TEST_F(StreamSinkTest, ServiceNormalDisconnect) {
  bool disconnected = false;
  executor().schedule_task(service_under_test().WhenDisconnected().then(
      [&disconnected](fpromise::result<void, zx_status_t>& result) {
        EXPECT_TRUE(result.is_ok());
        disconnected = true;
      }));

  RunLoopUntilIdle();

  // Expect that nothing has happened yet.
  EXPECT_FALSE(disconnected);

  // Disconnect.
  EXPECT_TRUE(!!service_under_test().Disconnect());
  RunLoopUntilIdle();

  // Expect that the disconnect task ran, because |Disconnect| was called.
  EXPECT_TRUE(disconnected);

  disconnected = false;
  executor().schedule_task(service_under_test().WhenDisconnected().then(
      [&disconnected](fpromise::result<void, zx_status_t>& result) {
        EXPECT_TRUE(result.is_ok());
        disconnected = true;
      }));

  RunLoopUntilIdle();

  // Expect that the disconnect task ran, because the service was already disconnected.
  EXPECT_TRUE(disconnected);
}

// Tests that disconnection notifications work on the service side when the channel is closed.
TEST_F(StreamSinkTest, ServiceSurpriseDisconnect) {
  bool disconnected = false;
  executor().schedule_task(service_under_test().WhenDisconnected().then(
      [&disconnected](fpromise::result<void, zx_status_t>& result) {
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.error());
        disconnected = true;
      }));

  RunLoopUntilIdle();

  // Expect that nothing has happened yet.
  EXPECT_FALSE(disconnected);

  // Disconnect the client, closing the channel from that end.
  EXPECT_TRUE(!!client_under_test().Disconnect());
  RunLoopUntilIdle();

  // Expect that the disconnect notification task ran.
  EXPECT_TRUE(disconnected);
}

}  // namespace test
}  // namespace fmlib
