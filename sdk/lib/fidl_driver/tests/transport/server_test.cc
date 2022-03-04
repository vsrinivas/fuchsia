// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fit/defer.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"

TEST(Server, OnUnboundFnCalledOnClientReset) {
  fidl_driver_testing::ScopedFakeDriver driver;

  auto dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED);
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
}
