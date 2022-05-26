// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/gtest/real_loop_fixture.h>

#include "src/media/vnext/lib/stream_io/input.h"
#include "src/media/vnext/lib/stream_io/packet.h"
#include "src/media/vnext/lib/stream_io/test/fake_buffer_provider.h"

namespace fmlib::test {

constexpr uint32_t kRequestedBufferCount = 2;
constexpr uint32_t kExpectedBufferCount = kRequestedBufferCount + 1;
constexpr uint32_t kMinBufferSize = 1000;
constexpr int64_t kTimestamp = 1234;
constexpr uint32_t kEndsToSend = 1000;

class InputUnitTest : public gtest::RealLoopFixture {
 public:
  InputUnitTest() : executor_(dispatcher()) {
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

  // Returns a handler for a |Input::Connect| handler that expects the connection to succeed.
  static fit::function<
      void(fpromise::result<std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection>,
                            fuchsia::media2::ConnectionError>&)>
  InputConnectionHandler(std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection>& connection) {
    return
        [&connection](fpromise::result<std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection>,
                                       fuchsia::media2::ConnectionError>& result) {
          EXPECT_TRUE(result.is_ok());
          connection = result.take_value();
          EXPECT_TRUE(connection);
        };
  }

  async::Executor& executor() { return executor_; }

  fuchsia::media2::BufferProvider& buffer_provider() { return *buffer_provider_; }

  // Creates a buffer collection.
  void CreateBufferCollection(zx::eventpair provider_token, bool& completed,
                              uint32_t expected_buffer_count = kExpectedBufferCount,
                              uint32_t expected_buffer_size = kMinBufferSize) {
    completed = false;
    buffer_provider_->CreateBufferCollection(
        std::move(provider_token), "input unittests",
        [&completed, expected_buffer_count, expected_buffer_size](
            fuchsia::media2::BufferProvider_CreateBufferCollection_Result result) mutable {
          EXPECT_TRUE(result.is_response());
          EXPECT_EQ(expected_buffer_count, result.response().collection_info.buffer_count());
          EXPECT_EQ(expected_buffer_size, result.response().collection_info.buffer_size());
          completed = true;
        });
  }

  fuchsia::media2::StreamSinkHandle ConnectInput(
      Input<std::unique_ptr<Packet>>& input,
      std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection>& connection_out) {
    zx::eventpair provider_token;
    zx::eventpair input_token;
    CreateBufferCollectionTokens(provider_token, input_token);

    // Start connecting.
    fuchsia::media2::StreamSinkHandle handle;
    executor().schedule_task(input
                                 .Connect(Thread::CreateForLoop(loop()), handle.NewRequest(),
                                          buffer_provider(), std::move(input_token),
                                          SimpleConstraints())
                                 .then(InputConnectionHandler(connection_out)));

    bool create_buffer_collection_completed;
    CreateBufferCollection(std::move(provider_token), create_buffer_collection_completed);

    // Both the promise and the |CreateBufferCollection| callback should complete now.
    RunLoopUntilIdle();
    EXPECT_TRUE(connection_out);
    EXPECT_TRUE(create_buffer_collection_completed);
    EXPECT_TRUE(connection_out->is_connected());

    return handle;
  }

 private:
  async::Executor executor_;
  std::unique_ptr<fuchsia::media2::BufferProvider> buffer_provider_;
};

// Test basic input connection.
TEST_F(InputUnitTest, Connect) {
  Input<std::unique_ptr<Packet>> under_test;

  fuchsia::media2::StreamSinkHandle handle;

  zx::eventpair provider_token;
  zx::eventpair input_token;
  CreateBufferCollectionTokens(provider_token, input_token);

  // Start connecting.
  std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection> connection;
  executor().schedule_task(under_test
                               .Connect(Thread::CreateForLoop(loop()), handle.NewRequest(),
                                        buffer_provider(), std::move(input_token),
                                        SimpleConstraints())
                               .then(InputConnectionHandler(connection)));

  // The promise shouldn't complete until we create the buffer collection.
  RunLoopUntilIdle();
  EXPECT_FALSE(connection);

  bool create_buffer_collection_completed;
  CreateBufferCollection(std::move(provider_token), create_buffer_collection_completed);

  // Both the promise and the |CreateBufferCollection| callback should complete now.
  RunLoopUntilIdle();
  EXPECT_TRUE(connection);
  EXPECT_TRUE(create_buffer_collection_completed);
  EXPECT_TRUE(connection->is_connected());
}

// Test |WhenDisconnected| response to already being disconnected.
TEST_F(InputUnitTest, AlreadyDisconnected) {
  Input<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection> connection;

  // We're discarding the interface handle here, which closes the channel.
  (void)ConnectInput(under_test, connection);

  RunLoopUntilIdle();
  EXPECT_FALSE(connection->is_connected());

  bool disconnected = false;
  executor().schedule_task(connection->WhenDisconnected().then(
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
TEST_F(InputUnitTest, PeerDisconnect) {
  Input<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection> connection;
  auto handle = ConnectInput(under_test, connection);

  bool disconnected = false;
  executor().schedule_task(connection->WhenDisconnected().then(
      [&disconnected](const fpromise::result<void, zx_status_t>& result) {
        // Promise should failed with epitaph.
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.error());
        disconnected = true;
      }));

  // The |WhenDisconnected| promise should complete with |ZX_ERR_PEER_CLOSED|.
  handle = nullptr;

  RunLoopUntilIdle();
  EXPECT_FALSE(connection->is_connected());
  EXPECT_TRUE(disconnected);
}

// Test |WhenDisconnected| response to explicit disconnect.
TEST_F(InputUnitTest, LocalDisconnect) {
  Input<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection> connection;
  auto handle = ConnectInput(under_test, connection);

  bool handler_deleted = false;
  auto deferred = fit::defer([&handler_deleted]() { handler_deleted = true; });
  executor().schedule_task(connection->WhenDisconnected().then(
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

// Test packet/signal delivery.
TEST_F(InputUnitTest, DataFlow) {
  Input<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection> connection;
  fuchsia::media2::StreamSinkPtr ptr = ConnectInput(under_test, connection).Bind();

  // Get a pull pending to receive the cleared signal.
  bool pull_completed = false;
  executor().schedule_task(connection->Pull().then(
      [&pull_completed](Input<std::unique_ptr<Packet>>::PullResult& result) {
        EXPECT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().is_clear_request());
        pull_completed = true;
      }));

  // Expect pull hasn't completed, because we haven't sent anything.
  RunLoopUntilIdle();
  EXPECT_FALSE(pull_completed);

  zx::eventpair completion_fence_client;
  zx::eventpair completion_fence_service;
  zx_status_t status =
      zx::eventpair::create(0, &completion_fence_client, &completion_fence_service);
  EXPECT_EQ(ZX_OK, status);

  // Send a cleared signal.
  ptr->Clear(true, std::move(completion_fence_service));

  // Expect pull has completed returning a cleared signal.
  RunLoopUntilIdle();
  EXPECT_TRUE(pull_completed);

  // Send an ended signal.
  ptr->End();

  // Pull the ended signal.
  pull_completed = false;
  executor().schedule_task(connection->Pull().then(
      [&pull_completed](Input<std::unique_ptr<Packet>>::PullResult& result) {
        EXPECT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().is_ended());
        pull_completed = true;
      }));

  // Expect pull has completed, because we already sent an ended signal.
  RunLoopUntilIdle();
  EXPECT_TRUE(pull_completed);

  // Get a pull pending to receive the packet.
  std::unique_ptr<Packet> received_packet;
  executor().schedule_task(connection->Pull().then(
      [&received_packet](Input<std::unique_ptr<Packet>>::PullResult& result) {
        EXPECT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().is_packet());
        received_packet = result.value().take_packet();
      }));

  // Expect pull hasn't completed, because we haven't sent the packet yet.
  RunLoopUntilIdle();
  EXPECT_FALSE(received_packet);

  // Send the packet.
  fuchsia::media2::Packet sent_packet{
      .payload = {{.buffer_id = 0, .offset = 0, .size = kMinBufferSize}},
      .timestamp = fuchsia::media2::PacketTimestamp::WithSpecified(kTimestamp + 0)};
  zx::eventpair release_fence_local;
  zx::eventpair release_fence_remote;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &release_fence_local, &release_fence_remote));
  ptr->PutPacket(std::move(sent_packet), std::move(release_fence_remote));

  // Expect pull has completed returning a packet.
  RunLoopUntilIdle();
  EXPECT_TRUE(received_packet);
  EXPECT_EQ(0u, received_packet->payload_range().buffer_id);
  EXPECT_EQ(0u, received_packet->payload_range().offset);
  EXPECT_EQ(kMinBufferSize, received_packet->payload_range().size);
  EXPECT_EQ(kMinBufferSize, received_packet->size());
  EXPECT_TRUE(!!received_packet->data());
  EXPECT_TRUE(received_packet->timestamp().is_specified());
  EXPECT_EQ(kTimestamp, received_packet->timestamp().specified());

  bool release_fence_local_peer_closed = false;
  executor().schedule_task(
      executor()
          .MakePromiseWaitHandle(zx::unowned_handle(release_fence_local.get()),
                                 ZX_EVENTPAIR_PEER_CLOSED)
          .then([&release_fence_local_peer_closed](
                    fpromise::result<zx_packet_signal_t, zx_status_t>& result) {
            EXPECT_TRUE(result.is_ok());
            EXPECT_EQ(ZX_EVENTPAIR_PEER_CLOSED, result.value().trigger);
            release_fence_local_peer_closed = true;
          }));

  // Expect the local release fence has not gotten |ZX_EVENTPAIR_PEER_CLOSED| yet.
  RunLoopUntilIdle();
  EXPECT_FALSE(release_fence_local_peer_closed);

  // Destroy the received packet.
  received_packet.reset();

  // Expect the local release fence has gotten |ZX_EVENTPAIR_PEER_CLOSED|.
  RunLoopUntilIdle();
  EXPECT_TRUE(release_fence_local_peer_closed);
}
// Test transition from one connection to another.
TEST_F(InputUnitTest, Transition) {
  Input<std::unique_ptr<Packet>> under_test;
  std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection> connection_a;
  fuchsia::media2::StreamSinkPtr ptr_a = ConnectInput(under_test, connection_a).Bind();

  // Send a bunch of end signals.
  for (uint32_t i = 0; i < kEndsToSend; ++i) {
    ptr_a->End();
  }

  zx::eventpair provider_token;
  zx::eventpair input_token;
  CreateBufferCollectionTokens(provider_token, input_token);

  // Create a second connection.
  std::unique_ptr<Input<std::unique_ptr<Packet>>::Connection> connection_b;
  fuchsia::media2::StreamSinkPtr ptr_b;
  executor().schedule_task(under_test
                               .Connect(Thread::CreateForLoop(loop()), ptr_b.NewRequest(),
                                        buffer_provider(), std::move(input_token),
                                        SimpleConstraints())
                               .then(InputConnectionHandler(connection_b)));

  bool create_buffer_collection_completed;
  CreateBufferCollection(std::move(provider_token), create_buffer_collection_completed);

  RunLoopUntilIdle();
  EXPECT_TRUE(create_buffer_collection_completed);
  EXPECT_FALSE(connection_b);

  // Consume the end signals.
  for (uint32_t i = 0; i < kEndsToSend; ++i) {
    bool pull_completed = false;
    executor().schedule_task(connection_a->Pull().then(
        [&pull_completed](Input<std::unique_ptr<Packet>>::PullResult& result) {
          EXPECT_TRUE(result.is_ok());
          EXPECT_TRUE(result.value().is_ended());
          pull_completed = true;
        }));

    RunLoopUntilIdle();
    EXPECT_TRUE(pull_completed);
  }

  // Pull one more time expecting disconnect. This shouldn't complete until |ptr_a| is closed.
  bool pull_completed = false;
  executor().schedule_task(connection_a->Pull().then(
      [&pull_completed](Input<std::unique_ptr<Packet>>::PullResult& result) {
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ(InputError::kDisconnected, result.error());
        pull_completed = true;
      }));

  RunLoopUntilIdle();
  EXPECT_FALSE(pull_completed);

  // Close the first connection.
  ptr_a = nullptr;

  // Expect that we got the disconnect on the old connection and that the new connection is ready.
  RunLoopUntilIdle();
  EXPECT_TRUE(pull_completed);
  EXPECT_TRUE(connection_b);

  if (!connection_b) {
    return;
  }

  // Make sure the new connection works.
  ptr_b->End();
  pull_completed = false;
  executor().schedule_task(connection_b->Pull().then(
      [&pull_completed](Input<std::unique_ptr<Packet>>::PullResult& result) {
        EXPECT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().is_ended());
        pull_completed = true;
      }));

  RunLoopUntilIdle();
  EXPECT_TRUE(pull_completed);
}

}  // namespace fmlib::test
