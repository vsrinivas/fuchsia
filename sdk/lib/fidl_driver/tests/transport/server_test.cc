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

TEST(Server, OnUnboundFnCalledOnClientReset) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::EmptyProtocol> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::EmptyProtocol> client_end(std::move(channels->end1));

  fdf::UnownedChannel server_handle = server_end.handle();

  class TestServer : public fdf::WireServer<test_transport::EmptyProtocol> {};
  auto server = std::make_shared<TestServer>();

  sync_completion_t completion;
  auto on_unbound = [&](TestServer* test_server, fidl::UnbindInfo unbind_info,
                        fdf::ServerEnd<test_transport::EmptyProtocol> server_end) {
    EXPECT_EQ(server.get(), test_server);
    EXPECT_TRUE(unbind_info.is_peer_closed());
    EXPECT_EQ(server_handle->get(), server_end.handle()->get());
    sync_completion_signal(&completion);
  };

  fdf::BindServer(dispatcher->get(), std::move(server_end), server, std::move(on_unbound));

  client_end.reset();

  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

// This test tears down the binding from within a message handler.
// Within a message handler there is no active async channel read
// operation, so it exercises slightly different code paths than
// tearing down from some posted task or tearing down from an
// arbitrary thread.
TEST(Server, UnbindInMethodHandler) {
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

  struct TestServer : public fdf::WireServer<test_transport::TwoWayTest> {
   public:
    void TwoWay(TwoWayRequestView request, fdf::Arena& in_request_arena,
                TwoWayCompleter::Sync& completer) override {
      ZX_ASSERT(binding_ref.has_value());
      binding_ref->Unbind();
    }

    std::optional<fdf::ServerBindingRef<test_transport::TwoWayTest>> binding_ref;
  };

  auto server = std::make_shared<TestServer>();
  fdf::ServerBindingRef binding_ref =
      fdf::BindServer(server_dispatcher->get(), std::move(server_end), server,
                      fidl_driver_testing::FailTestOnServerError<test_transport::TwoWayTest>());
  server->binding_ref = binding_ref;

  fdf::Arena arena('TEST');

  libsync::Completion sync_call;
  auto run_on_dispatcher_thread = [&] {
    fdf::WireSyncClient<test_transport::TwoWayTest> client(std::move(client_end));
    fdf::WireUnownedResult<test_transport::TwoWayTest::TwoWay> result =
        client.buffer(arena)->TwoWay(100);
    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error().is_peer_closed());
    sync_call.Signal();
  };
  async::PostTask(client_dispatcher->async_dispatcher(), run_on_dispatcher_thread);
  ASSERT_OK(sync_call.Wait());

  client_dispatcher->ShutdownAsync();
  server_dispatcher->ShutdownAsync();
  ASSERT_OK(client_dispatcher_shutdown.Wait());
  ASSERT_OK(server_dispatcher_shutdown.Wait());
}

}  // namespace
