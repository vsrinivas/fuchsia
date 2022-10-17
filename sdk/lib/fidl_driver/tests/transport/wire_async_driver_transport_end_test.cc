// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/assert_peer_closed_helper.h"
#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"
#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

class TestServer : public fdf::WireServer<test_transport::SendDriverTransportEndTest> {
  void SendDriverTransportEnd(SendDriverTransportEndRequestView request, fdf::Arena& arena,
                              SendDriverTransportEndCompleter::Sync& completer) override {
    completer.buffer(arena).Reply(std::move(request->c), std::move(request->s));
  }
};

TEST(DriverTransport, WireSendDriverClientEnd) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::SendDriverTransportEndTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::SendDriverTransportEndTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(
      dispatcher->get(), std::move(server_end), server,
      fidl_driver_testing::FailTestOnServerError<test_transport::SendDriverTransportEndTest>());

  fdf::WireSharedClient<test_transport::SendDriverTransportEndTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  fdf::Arena arena('TEST');

  auto endpoints = fdf::CreateEndpoints<test_transport::OneWayTest>();
  fidl_handle_t client_handle = endpoints->client.handle()->get();
  fidl_handle_t server_handle = endpoints->server.handle()->get();

  sync_completion_t done;
  client.buffer(arena)
      ->SendDriverTransportEnd(std::move(endpoints->client), std::move(endpoints->server))
      .ThenExactlyOnce(
          [&](fdf::WireUnownedResult<
              ::test_transport::SendDriverTransportEndTest::SendDriverTransportEnd>& result) {
            ASSERT_OK(result.status());
            ASSERT_TRUE(result->c.is_valid());
            ASSERT_EQ(client_handle, result->c.handle()->get());
            ASSERT_TRUE(result->s.is_valid());
            ASSERT_EQ(server_handle, result->s.handle()->get());
            sync_completion_signal(&done);
          });

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

TEST(DriverTransport, WireSendDriverClientEndEncodeErrorShouldCloseHandle) {
  fidl_driver_testing::ScopedFakeDriver driver;
  libsync::Completion dispatcher_shutdown;
  zx::result dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());
  zx::result endpoints = fdf::CreateEndpoints<test_transport::OnErrorCloseHandlesTest>();
  ASSERT_OK(endpoints.status_value());
  fdf::Arena arena('TEST');

  fdf::WireSharedClient client(std::move(endpoints->client), dispatcher->get());

  zx::result send_endpoints = fdf::CreateEndpoints<test_transport::OneWayTest>();
  ASSERT_OK(send_endpoints.status_value());

  fidl::Status status =
      client.buffer(arena)->SendDriverClientEnd("too long", std::move(send_endpoints->client));
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(fidl::Reason::kEncodeError, status.reason());
  ASSERT_NO_FAILURES(fidl_driver_testing::AssertPeerClosed(*send_endpoints->server.handle()));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

}  // namespace
