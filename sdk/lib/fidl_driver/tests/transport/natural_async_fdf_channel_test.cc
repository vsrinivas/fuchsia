// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/fidl.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"
#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

class TestServer : public fdf::Server<test_transport::SendFdfChannelTest> {
  void SendFdfChannel(SendFdfChannelRequest& request,
                      SendFdfChannelCompleter::Sync& completer) override {
    completer.Reply(std::move(request.h()));
  }
};

TEST(DriverTransport, NaturalSendFdfChannelAsync) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::SendFdfChannelTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::SendFdfChannelTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server,
                  fidl_driver_testing::FailTestOnServerError<test_transport::SendFdfChannelTest>());

  fdf::SharedClient<test_transport::SendFdfChannelTest> client;
  client.Bind(std::move(client_end), dispatcher->get());

  auto channel_pair = fdf::ChannelPair::Create(0);
  fdf_handle_t handle = channel_pair->end0.get();

  libsync::Completion completion;
  client->SendFdfChannel(std::move(channel_pair->end0))
      .ThenExactlyOnce(
          [&completion,
           handle](fdf::Result<::test_transport::SendFdfChannelTest::SendFdfChannel>& result) {
            ASSERT_TRUE(result.is_ok());
            ASSERT_TRUE(result->h().is_valid());
            ASSERT_EQ(handle, result->h().get());
            completion.Signal();
          });

  ASSERT_OK(completion.Wait());

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

}  // namespace
