// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/defer.h>
#include <lib/gtest/real_loop_fixture.h>

#include "src/media/vnext/lib/stream_io/output.h"
#include "src/media/vnext/lib/stream_io/packet.h"
#include "src/media/vnext/lib/stream_io/test/fake_buffer_provider.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib::test {

constexpr uint32_t kRequestedBufferCount = 2;
constexpr uint32_t kExpectedBufferCount = kRequestedBufferCount + 1;
constexpr uint32_t kMinBufferSize = 1000;
constexpr int64_t kTimestamp = 1234;
constexpr uint32_t kEndsToSend = 1000;

class OutputUnitTest : public gtest::RealLoopFixture {
 public:
  OutputUnitTest() : thread_(fmlib::Thread::CreateForLoop(loop())) {
    buffer_provider_ = std::make_unique<FakeBufferProvider>();
  }

 protected:
  // Creates a pair of buffer collection tokens.
  static void CreateBufferCollectionTokens(zx::eventpair& provider_token_out,
                                           zx::eventpair& participant_token_out) {
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &provider_token_out, &participant_token_out));
  }

  // Creates buffer collection tokens, one for the provider and two for the participants.
  static void CreateBufferCollectionTokens(zx::eventpair& provider_token_out,
                                           zx::eventpair& participant_a_token_out,
                                           zx::eventpair& participant_b_token_out) {
    CreateBufferCollectionTokens(provider_token_out, participant_a_token_out);
    EXPECT_EQ(ZX_OK,
              participant_a_token_out.duplicate(ZX_RIGHT_SAME_RIGHTS, &participant_b_token_out));
  }

  // Returns a |fuchsia::media2::BufferConstraints| with |kRequestedBufferCount| and
  // |kMinBufferSize|.
  static fuchsia::media2::BufferConstraints SimpleConstraints() {
    fuchsia::media2::BufferConstraints constraints;
    constraints.set_buffer_count(kRequestedBufferCount);
    constraints.set_min_buffer_size(kMinBufferSize);
    return constraints;
  }

  // Returns a handler for a |Output::Connect| handler that expects the connection to succeed.
  static fit::function<
      void(fpromise::result<std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection>,
                            fuchsia::media2::ConnectionError>&)>
  OutputConnectionHandler(
      std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection>& connection) {
    return
        [&connection](fpromise::result<std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection>,
                                       fuchsia::media2::ConnectionError>& result) {
          EXPECT_TRUE(result.is_ok());
          connection = result.take_value();
          EXPECT_TRUE(connection);
        };
  }

  Thread thread() { return thread_; }

  fuchsia::media2::BufferProvider& buffer_provider() { return *buffer_provider_; }

  // Creates a buffer collection.
  void CreateBufferCollection(zx::eventpair provider_token, bool& completed,
                              uint32_t expected_buffer_count = kExpectedBufferCount,
                              uint32_t expected_buffer_size = kMinBufferSize) {
    completed = false;
    buffer_provider_->CreateBufferCollection(
        std::move(provider_token), "output unittests",
        [&completed, expected_buffer_count, expected_buffer_size](
            fuchsia::media2::BufferProvider_CreateBufferCollection_Result result) mutable {
          EXPECT_TRUE(result.is_response());
          EXPECT_EQ(expected_buffer_count, result.response().collection_info.buffer_count());
          EXPECT_EQ(expected_buffer_size, result.response().collection_info.buffer_size());
          completed = true;
        });
  }

  fidl::InterfaceRequest<fuchsia::media2::StreamSink> ConnectOutput(
      Output<std::unique_ptr<Packet>>& output,
      std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection>& connection_out) {
    zx::eventpair provider_token;
    zx::eventpair output_token;
    CreateBufferCollectionTokens(provider_token, output_token);

    // Start connecting.
    fuchsia::media2::StreamSinkHandle handle;
    auto request = handle.NewRequest();
    thread().schedule_task(output
                               .Connect(thread(), std::move(handle), buffer_provider(),
                                        std::move(output_token), SimpleConstraints())
                               .then(OutputConnectionHandler(connection_out)));

    bool create_buffer_collection_completed;
    CreateBufferCollection(std::move(provider_token), create_buffer_collection_completed);

    // Both the promise and the |CreateBufferCollection| callback should complete now.
    RunLoopUntilIdle();
    EXPECT_TRUE(connection_out);
    EXPECT_TRUE(create_buffer_collection_completed);
    EXPECT_TRUE(connection_out || connection_out->is_connected());

    return request;
  }

 private:
  Thread thread_;
  std::unique_ptr<FakeBufferProvider> buffer_provider_;
};

class FakeStreamSink : public fuchsia::media2::StreamSink {
 public:
  struct PacketInfo {
    PacketInfo(fuchsia::media2::Packet packet, zx::eventpair release_fence)
        : packet_(std::move(packet)), release_fence_(std::move(release_fence)) {}

    fuchsia::media2::Packet packet_;
    zx::eventpair release_fence_;
  };

  enum class Other {
    kEnded,
  };
  using Received = std::variant<PacketInfo, ClearRequest, Other>;

  FakeStreamSink() : binding_(this) {}

  ~FakeStreamSink() override = default;

  void Bind(fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t status) {
      EXPECT_NE(ZX_OK, status);
      connection_error_ = status;
    });
  }

  std::queue<Received>& received() { return received_; }

  zx_status_t connection_error() const { return connection_error_; }

  // fuchsia::media2::StreamSink implementation.
  void PutPacket(fuchsia::media2::Packet packet, zx::eventpair release_fence) override {
    received_.push(PacketInfo(std::move(packet), std::move(release_fence)));
  }

  void End() override { received_.push(Other::kEnded); }

  void Clear(bool hold_last_frame, zx::handle completion_fence) override {
    // Note we don't actually clear here. |received_| is a log of what has arrived over a
    // |StreamSink| channel, not a real |StreamQueue|.
    received_.push(ClearRequest(hold_last_frame, zx::eventpair(completion_fence.get())));
  }

 private:
  std::queue<Received> received_;
  fidl::Binding<fuchsia::media2::StreamSink> binding_;
  zx_status_t connection_error_ = ZX_OK;
};

// Test basic output connection.
TEST_F(OutputUnitTest, Connect) {
  Output<std::unique_ptr<Packet>> under_test;

  fuchsia::media2::StreamSinkHandle handle;
  auto request = handle.NewRequest();

  zx::eventpair provider_token;
  zx::eventpair output_token;
  CreateBufferCollectionTokens(provider_token, output_token);

  // Start connecting.
  std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection> connection;
  thread().schedule_task(under_test
                             .Connect(thread(), std::move(handle), buffer_provider(),
                                      std::move(output_token), SimpleConstraints())
                             .then(OutputConnectionHandler(connection)));

  // The promise shouldn't complete until we create the buffer collection.
  RunLoopUntilIdle();
  EXPECT_FALSE(connection);

  bool create_buffer_collection_completed;
  CreateBufferCollection(std::move(provider_token), create_buffer_collection_completed);

  // Both the promise and the |CreateBufferCollection| callback should complete now.
  RunLoopUntilIdle();
  EXPECT_TRUE(connection);
  EXPECT_TRUE(create_buffer_collection_completed);
  EXPECT_TRUE(!connection || connection->is_connected());
}

// Test |WhenDisconnected| response to already being disconnected.
TEST_F(OutputUnitTest, AlreadyDisconnected) {
  Output<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection> connection;
  auto request = ConnectOutput(under_test, connection);

  EXPECT_EQ(ZX_OK, request.Close(ZX_ERR_PEER_CLOSED));
  RunLoopUntilIdle();
  EXPECT_FALSE(connection->is_connected());

  bool disconnected = false;
  thread().schedule_task(connection->WhenDisconnected().then(
      [&disconnected](const fpromise::result<void, zx_status_t>& result) {
        // Promise should succeed.
        EXPECT_TRUE(result.is_ok());
        disconnected = true;
      }));

  RunLoopUntilIdle();
  EXPECT_FALSE(connection->is_connected());
  EXPECT_TRUE(disconnected);
}

// Test |WhenDisconnected| response to peer disconnect.
TEST_F(OutputUnitTest, PeerDisconnect) {
  Output<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection> connection;
  auto request = ConnectOutput(under_test, connection);

  bool disconnected = false;
  thread().schedule_task(connection->WhenDisconnected().then(
      [&disconnected](const fpromise::result<void, zx_status_t>& result) {
        // Promise should failed with epitaph.
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(ZX_ERR_UNAVAILABLE, result.error());
        disconnected = true;
      }));

  // The |WhenDisconnected| promise should complete with this error.
  EXPECT_EQ(ZX_OK, request.Close(ZX_ERR_UNAVAILABLE));

  RunLoopUntilIdle();
  EXPECT_FALSE(connection->is_connected());
  EXPECT_TRUE(disconnected);
}

// Test |WhenDisconnected| response to explicit disconnect.
TEST_F(OutputUnitTest, LocalDisconnect) {
  Output<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection> connection;
  auto request = ConnectOutput(under_test, connection);

  bool handler_deleted = false;
  auto deferred = fit::defer([&handler_deleted]() { handler_deleted = true; });
  thread().schedule_task(connection->WhenDisconnected().then(
      [deferred = std::move(deferred)](const fpromise::result<void, zx_status_t>& result) {
        // This handler is not expected to run.
        EXPECT_TRUE(false);
      }));

  RunLoopUntilIdle();
  EXPECT_FALSE(handler_deleted);

  // Disconnect.
  connection.reset();

  // Expect that the handler is deleted now.
  RunLoopUntilIdle();
  EXPECT_TRUE(handler_deleted);
}

// Test buffer allocation.
TEST_F(OutputUnitTest, BufferAllocation) {
  Output<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection> connection;
  auto request = ConnectOutput(under_test, connection);

  bool buffer_available = false;
  thread().schedule_task(
      connection->buffer_collection()
          .AllocatePayloadBufferWhenAvailable(kMinBufferSize)
          .then([&buffer_available](const fpromise::result<PayloadBuffer>& result) {
            EXPECT_TRUE(result.is_ok());
            buffer_available = true;
            return fpromise::ok();
          }));

  // Expect a buffer to be available now.
  RunLoopUntilIdle();
  EXPECT_TRUE(buffer_available);

  std::vector<PayloadBuffer> payload_buffers;
  for (uint32_t i = 0; i < kExpectedBufferCount; ++i) {
    payload_buffers.push_back(
        connection->buffer_collection().AllocatePayloadBuffer(kMinBufferSize));
    EXPECT_TRUE(payload_buffers.back());
  }

  // Expect buffers to be exhausted.
  EXPECT_FALSE(connection->buffer_collection().AllocatePayloadBuffer(kMinBufferSize));

  buffer_available = false;
  thread().schedule_task(
      connection->buffer_collection()
          .AllocatePayloadBufferWhenAvailable(kMinBufferSize)
          .then([&buffer_available](const fpromise::result<PayloadBuffer>& result) {
            EXPECT_TRUE(result.is_ok());
            buffer_available = true;
            return fpromise::ok();
          }));

  // Expect no buffer to be available now.
  RunLoopUntilIdle();
  EXPECT_FALSE(buffer_available);

  // Discard a buffer.
  payload_buffers.pop_back();

  // Expect a buffer to be available now.
  RunLoopUntilIdle();
  EXPECT_TRUE(buffer_available);

  // Allocate another buffer.
  payload_buffers.push_back(connection->buffer_collection().AllocatePayloadBuffer(kMinBufferSize));
  EXPECT_TRUE(payload_buffers.back());

  // Expect buffers to be exhausted again.
  EXPECT_FALSE(connection->buffer_collection().AllocatePayloadBuffer(kMinBufferSize));
}

// Test packet/signal delivery.
TEST_F(OutputUnitTest, DataFlow) {
  Output<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection> connection;
  FakeStreamSink stream_sink;
  stream_sink.Bind(ConnectOutput(under_test, connection));

  EXPECT_TRUE(stream_sink.received().empty());

  zx::eventpair completion_fence_client;
  zx::eventpair completion_fence_service;
  zx_status_t status =
      zx::eventpair::create(0, &completion_fence_client, &completion_fence_service);
  EXPECT_EQ(ZX_OK, status);

  // Send clear and end.
  connection->Clear(true, std::move(completion_fence_service));
  connection->End();

  // Allocate all the buffers. We'll expect to get one back later.
  std::vector<PayloadBuffer> payload_buffers;
  for (uint32_t i = 0; i < kExpectedBufferCount; ++i) {
    payload_buffers.push_back(
        connection->buffer_collection().AllocatePayloadBuffer(kMinBufferSize));
    EXPECT_TRUE(payload_buffers.back());
  }

  // Send a packet.
  connection->Push(std::make_unique<Packet>(std::move(payload_buffers.back()), kTimestamp));

  // Expect cleared, ended and a packet.
  RunLoopUntilIdle();
  EXPECT_FALSE(stream_sink.received().empty());

  EXPECT_EQ(1u, stream_sink.received().front().index());
  stream_sink.received().pop();

  EXPECT_EQ(2u, stream_sink.received().front().index());
  EXPECT_EQ(FakeStreamSink::Other::kEnded, std::get<2>(stream_sink.received().front()));
  stream_sink.received().pop();

  {
    EXPECT_EQ(0u, stream_sink.received().front().index());
    auto packet_info = std::move(std::get<0>(stream_sink.received().front()));
    EXPECT_EQ(1u, packet_info.packet_.payload.size());
    EXPECT_EQ(0u, packet_info.packet_.payload[0].offset);
    EXPECT_EQ(kMinBufferSize, packet_info.packet_.payload[0].size);
    EXPECT_TRUE(packet_info.packet_.timestamp.is_specified());
    EXPECT_EQ(kTimestamp, packet_info.packet_.timestamp.specified());
    stream_sink.received().pop();

    EXPECT_TRUE(stream_sink.received().empty());

    // Expect buffers to be exhausted.
    RunLoopUntilIdle();
    EXPECT_FALSE(connection->buffer_collection().AllocatePayloadBuffer(kMinBufferSize));

    // |packet_info| goes out of scope here, deleting the release fence.
  }

  // Expect a buffer to be available.
  RunLoopUntilIdle();
  EXPECT_TRUE(connection->buffer_collection().AllocatePayloadBuffer(kMinBufferSize));

  EXPECT_EQ(ZX_OK, stream_sink.connection_error());
}

// Test |Output::DrainAndDisconnect|.
TEST_F(OutputUnitTest, DrainAndDisconnect) {
  Output<std::unique_ptr<Packet>> under_test;

  // Create a connection with |stream_sink_a| at the remote end.
  std::unique_ptr<Output<std::unique_ptr<Packet>>::Connection> connection;
  FakeStreamSink stream_sink_a;
  stream_sink_a.Bind(ConnectOutput(under_test, connection));

  EXPECT_TRUE(stream_sink_a.received().empty());

  // Send a bunch of end signals.
  for (uint32_t i = 0; i < kEndsToSend; ++i) {
    connection->End();
  }

  // Drain and disconnect the connection.
  thread().schedule_task(under_test.DrainAndDisconnect(std::move(connection)));

  // Create a new connection with |stream_sink_b| at the remote end.
  FakeStreamSink stream_sink_b;
  stream_sink_b.Bind(ConnectOutput(under_test, connection));

  // All the end signals should end up at |stream_sink_a|.
  RunLoopUntilIdle();
  EXPECT_EQ(kEndsToSend, stream_sink_a.received().size());
  EXPECT_TRUE(stream_sink_b.received().empty());

  // Expect |stream_sink_a| to have gotten PEER_CLOSED, while |stream_sink_b| remains connected.
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, stream_sink_a.connection_error());
  EXPECT_EQ(ZX_OK, stream_sink_b.connection_error());

  // Send one end signal.
  connection->End();

  // Expect that the end signal ended up at |stream_sink_b|.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, stream_sink_b.received().size());
}

}  // namespace fmlib::test
