// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/fidl.h>
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

class TestServer : public fdf::Server<test_transport::SendZirconHandleTest> {
  void SendZirconHandle(SendZirconHandleRequest& request,
                        SendZirconHandleCompleter::Sync& completer) override {
    completer.Reply(std::move(request.h()));
  }
};

TEST(DriverTransport, NaturalSendZirconHandleAsync) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::SendZirconHandleTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::SendZirconHandleTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(
      dispatcher->get(), std::move(server_end), server,
      fidl_driver_testing::FailTestOnServerError<test_transport::SendZirconHandleTest>());

  fdf::SharedClient<test_transport::SendZirconHandleTest> client;
  client.Bind(std::move(client_end), dispatcher->get());

  zx::event ev;
  zx::event::create(0, &ev);
  zx_handle_t handle = ev.get();

  libsync::Completion completion;
  client->SendZirconHandle(std::move(ev))
      .ThenExactlyOnce(
          [&completion,
           handle](fdf::Result<::test_transport::SendZirconHandleTest::SendZirconHandle>& result) {
            ASSERT_TRUE(result.is_ok());
            ASSERT_TRUE(result->h().is_valid());
            ASSERT_EQ(handle, result->h().get());
            completion.Signal();
          });

  ASSERT_OK(completion.Wait());

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

TEST(DriverTransport, NaturalSendZirconHandleEncodeErrorShouldCloseHandle) {
  fidl_driver_testing::ScopedFakeDriver driver;
  libsync::Completion dispatcher_shutdown;
  zx::result dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());
  zx::result endpoints = fdf::CreateEndpoints<test_transport::OnErrorCloseHandlesTest>();
  ASSERT_OK(endpoints.status_value());

  fdf::SharedClient client(std::move(endpoints->client), dispatcher->get());

  zx::channel c1, c2;
  ASSERT_OK(zx::channel::create(0, &c1, &c2));

  fit::result result = client->SendZirconHandle({{
      .s = "too long",
      .h = std::move(c1),
  }});
  ASSERT_FALSE(result.is_ok());
  ASSERT_EQ(fidl::Reason::kEncodeError, result.error_value().reason());
  ASSERT_NO_FAILURES(fidl_driver_testing::AssertPeerClosed(c2));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

}  // namespace
