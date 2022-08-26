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

struct TestServer : public fdf::WireServer<test_transport::SendFdfChannelTest> {
 public:
  explicit TestServer(libsync::Completion* destroyed) : destroyed_(destroyed) {}
  ~TestServer() override { destroyed_->Signal(); }

  void SendFdfChannel(SendFdfChannelRequestView request, fdf::Arena& arena,
                      SendFdfChannelCompleter::Sync& completer) override {
    completer.buffer(arena).Reply(std::move(request->h));
  }

 private:
  libsync::Completion* destroyed_;
};

TEST(DriverTransport, WireSendFdfChannelSync) {
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

  fdf::ServerEnd<test_transport::SendFdfChannelTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::SendFdfChannelTest> client_end(std::move(channels->end1));

  libsync::Completion server_destruction;
  auto server = std::make_shared<TestServer>(&server_destruction);
  fdf::ServerBindingRef binding_ref = fdf::BindServer(
      server_dispatcher->get(), std::move(server_end), server,
      fidl_driver_testing::FailTestOnServerError<test_transport::SendFdfChannelTest>());
  zx::status<fdf::Arena> arena = fdf::Arena::Create(0, 'TEST');
  ASSERT_OK(arena.status_value());

  auto channel_pair = fdf::ChannelPair::Create(0);
  fdf_handle_t handle = channel_pair->end0.get();

  auto run_on_dispatcher_thread = [&] {
    fdf::WireSyncClient<test_transport::SendFdfChannelTest> client(std::move(client_end));
    fdf::WireUnownedResult<test_transport::SendFdfChannelTest::SendFdfChannel> result =
        client.buffer(*arena)->SendFdfChannel(std::move(channel_pair->end0));
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->h.is_valid());
    ASSERT_EQ(handle, result->h.get());

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

}  // namespace
