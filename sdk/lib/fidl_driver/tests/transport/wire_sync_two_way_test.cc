// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
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
 public:
  explicit TestServer(libsync::Completion* destroyed) : destroyed_(destroyed) {}
  ~TestServer() override { destroyed_->Signal(); }

  void TwoWay(TwoWayRequestView request, fdf::Arena& in_request_arena,
              TwoWayCompleter::Sync& completer) override {
    ZX_ASSERT(request->payload == kRequestPayload);
    ASSERT_EQ(fdf_request_arena, in_request_arena.get());

    // Test using a different arena in the response.
    fdf::Arena response_arena('DIFF');
    fdf_response_arena = response_arena.get();
    completer.buffer(response_arena).Reply(kResponsePayload);
  }

  fdf_arena_t* fdf_request_arena;
  fdf_arena_t* fdf_response_arena;

 private:
  libsync::Completion* destroyed_;
};

TEST(DriverTransport, WireTwoWaySync) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion client_dispatcher_shutdown;
  auto client_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { client_dispatcher_shutdown.Signal(); });
  ASSERT_OK(client_dispatcher.status_value());

  libsync::Completion server_dispatcher_shutdown;
  auto server_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { server_dispatcher_shutdown.Signal(); });
  ASSERT_OK(server_dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayTest> client_end(std::move(channels->end1));

  libsync::Completion server_destruction;
  auto server = std::make_shared<TestServer>(&server_destruction);
  fdf::ServerBindingRef binding_ref =
      fdf::BindServer(server_dispatcher->get(), std::move(server_end), server,
                      fidl_driver_testing::FailTestOnServerError<test_transport::TwoWayTest>());
  fdf::Arena arena('ORIG');
  server->fdf_request_arena = arena.get();

  auto run_on_dispatcher_thread = [&] {
    fdf::WireSyncClient<test_transport::TwoWayTest> client(std::move(client_end));
    fdf::WireUnownedResult<test_transport::TwoWayTest::TwoWay> result =
        client.buffer(arena)->TwoWay(kRequestPayload);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(kResponsePayload, result->payload);
    ASSERT_EQ(server->fdf_response_arena, result.arena().get());

    // TODO(fxbug.dev/92489): If this call and wait is removed, the test will
    // flake by leaking |AsyncServerBinding| objects.
    binding_ref.Unbind();
    server.reset();
  };
  async::PostTask(client_dispatcher->async_dispatcher(), run_on_dispatcher_thread);
  ASSERT_OK(server_destruction.Wait());

  client_dispatcher->ShutdownAsync();
  server_dispatcher->ShutdownAsync();
  ASSERT_OK(client_dispatcher_shutdown.Wait());
  ASSERT_OK(server_dispatcher_shutdown.Wait());
}

TEST(DriverTransport, WireTwoWaySyncFreeFunction) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion client_dispatcher_shutdown;
  auto client_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { client_dispatcher_shutdown.Signal(); });
  ASSERT_OK(client_dispatcher.status_value());

  libsync::Completion server_dispatcher_shutdown;
  auto server_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { server_dispatcher_shutdown.Signal(); });
  ASSERT_OK(server_dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayTest> client_end(std::move(channels->end1));

  libsync::Completion server_destruction;
  auto server = std::make_shared<TestServer>(&server_destruction);
  fdf::ServerBindingRef binding_ref =
      fdf::BindServer(server_dispatcher->get(), std::move(server_end), server,
                      fidl_driver_testing::FailTestOnServerError<test_transport::TwoWayTest>());
  fdf::Arena arena('ORIG');
  server->fdf_request_arena = arena.get();

  auto run_on_dispatcher_thread = [&] {
    {
      fdf::WireUnownedResult<test_transport::TwoWayTest::TwoWay> result =
          fdf::WireCall(client_end).buffer(arena)->TwoWay(kRequestPayload);
      ASSERT_TRUE(result.ok());
      ASSERT_EQ(kResponsePayload, result->payload);
      ASSERT_EQ(server->fdf_response_arena, result.arena().get());
    }
    {
      fdf::WireUnownedResult<test_transport::TwoWayTest::TwoWay> result =
          fdf::WireCall(fdf::UnownedClientEnd<test_transport::TwoWayTest>(client_end))
              .buffer(arena)
              ->TwoWay(kRequestPayload);
      ASSERT_TRUE(result.ok());
      ASSERT_EQ(kResponsePayload, result->payload);
      ASSERT_EQ(server->fdf_response_arena, result.arena().get());
    }

    // TODO(fxbug.dev/92489): If this call and wait is removed, the test will
    // flake by leaking |AsyncServerBinding| objects.
    binding_ref.Unbind();
    server.reset();
  };
  async::PostTask(client_dispatcher->async_dispatcher(), run_on_dispatcher_thread);
  ASSERT_OK(server_destruction.Wait());

  client_dispatcher->ShutdownAsync();
  server_dispatcher->ShutdownAsync();
  ASSERT_OK(client_dispatcher_shutdown.Wait());
  ASSERT_OK(server_dispatcher_shutdown.Wait());
}

TEST(DriverTransport, WireTwoWaySyncViaAsyncClient) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  libsync::Completion client_dispatcher_shutdown;
  auto client_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { client_dispatcher_shutdown.Signal(); });
  ASSERT_OK(client_dispatcher.status_value());

  libsync::Completion server_dispatcher_shutdown;
  auto server_dispatcher = fdf::Dispatcher::Create(
      FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
      [&](fdf_dispatcher_t* dispatcher) { server_dispatcher_shutdown.Signal(); });
  ASSERT_OK(server_dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayTest> client_end(std::move(channels->end1));

  libsync::Completion server_destruction;
  auto server = std::make_shared<TestServer>(&server_destruction);
  fdf::ServerBindingRef binding_ref =
      fdf::BindServer(server_dispatcher->get(), std::move(server_end), server,
                      fidl_driver_testing::FailTestOnServerError<test_transport::TwoWayTest>());
  fdf::Arena arena('ORIG');
  server->fdf_request_arena = arena.get();

  auto run_on_dispatcher_thread = [&] {
    fdf::WireSharedClient<test_transport::TwoWayTest> client(std::move(client_end),
                                                             dispatcher->get());
    fdf::WireUnownedResult<test_transport::TwoWayTest::TwoWay> result =
        client.sync().buffer(arena)->TwoWay(kRequestPayload);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(kResponsePayload, result->payload);
    ASSERT_EQ(server->fdf_response_arena, result.arena().get());

    // TODO(fxbug.dev/92489): If this call and wait is removed, the test will
    // flake by leaking |AsyncServerBinding| objects.
    binding_ref.Unbind();
    server.reset();
  };
  async::PostTask(client_dispatcher->async_dispatcher(), run_on_dispatcher_thread);
  ASSERT_OK(server_destruction.Wait());

  dispatcher->ShutdownAsync();
  client_dispatcher->ShutdownAsync();
  server_dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
  ASSERT_OK(client_dispatcher_shutdown.Wait());
  ASSERT_OK(server_dispatcher_shutdown.Wait());
}

}  // namespace
