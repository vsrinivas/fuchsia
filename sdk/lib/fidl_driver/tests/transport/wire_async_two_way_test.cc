// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"
#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

constexpr uint32_t kRequestPayload = 1234;
constexpr uint32_t kResponsePayload = 5678;

struct TestServer : public fdf::WireServer<test_transport::TwoWayTest> {
  void TwoWay(TwoWayRequestView request, fdf::Arena& in_request_arena,
              TwoWayCompleter::Sync& completer) override {
    ASSERT_EQ(kRequestPayload, request->payload);
    ASSERT_EQ(fdf_request_arena, in_request_arena.get());

    // Test using a different arena in the response.
    fdf::Arena response_arena('DIFF');
    completer.buffer(response_arena).Reply(kResponsePayload);
    fdf_response_arena = std::move(response_arena);
  }

  fdf_arena_t* fdf_request_arena;
  fdf::Arena fdf_response_arena{nullptr};
};

TEST(DriverTransport, WireTwoWayAsync) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher = fdf::Dispatcher::Create(
      0, "", [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server,
                  fidl_driver_testing::FailTestOnServerError<::test_transport::TwoWayTest>());
  fdf::Arena arena('ORIG');
  server->fdf_request_arena = arena.get();

  fdf::WireClient<test_transport::TwoWayTest> client;
  sync_completion_t called;
  auto bind_and_run_on_dispatcher_thread = [&] {
    client.Bind(std::move(client_end), dispatcher->get());

    client.buffer(arena)
        ->TwoWay(kRequestPayload)
        .ThenExactlyOnce([&](fdf::WireUnownedResult<::test_transport::TwoWayTest::TwoWay>& result) {
          ASSERT_OK(result.status());
          ASSERT_EQ(kResponsePayload, result->payload);
          ASSERT_EQ(server->fdf_response_arena.get(), result.arena().get());
          sync_completion_signal(&called);
        });
  };
  async::PostTask(dispatcher->async_dispatcher(), bind_and_run_on_dispatcher_thread);
  ASSERT_OK(sync_completion_wait(&called, ZX_TIME_INFINITE));

  sync_completion_t destroyed;
  auto destroy_on_dispatcher_thread = [client = std::move(client), &destroyed] {
    sync_completion_signal(&destroyed);
  };
  async::PostTask(dispatcher->async_dispatcher(), std::move(destroy_on_dispatcher_thread));
  ASSERT_OK(sync_completion_wait(&destroyed, ZX_TIME_INFINITE));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

TEST(DriverTransport, WireTwoWayAsyncShared) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server,
                  fidl_driver_testing::FailTestOnServerError<::test_transport::TwoWayTest>());

  fdf::WireSharedClient<test_transport::TwoWayTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  fdf::Arena arena('ORIG');
  server->fdf_request_arena = arena.get();

  // Test |ThenExactlyOnce|.
  libsync::Completion done;
  client.buffer(arena)
      ->TwoWay(kRequestPayload)
      .ThenExactlyOnce(
          [&done, &server](fdf::WireUnownedResult<::test_transport::TwoWayTest::TwoWay>& result) {
            ASSERT_OK(result.status());
            ASSERT_EQ(kResponsePayload, result->payload);
            ASSERT_EQ(server->fdf_response_arena.get(), result.arena().get());
            done.Signal();
          });
  ASSERT_OK(done.Wait());

  // Test |Then|.
  done.Reset();
  client.buffer(arena)
      ->TwoWay(kRequestPayload)
      .Then([&done, &server](fdf::WireUnownedResult<::test_transport::TwoWayTest::TwoWay>& result) {
        ASSERT_OK(result.status());
        ASSERT_EQ(kResponsePayload, result->payload);
        ASSERT_EQ(server->fdf_response_arena.get(), result.arena().get());
        done.Signal();
      });
  ASSERT_OK(done.Wait());

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

}  // namespace
