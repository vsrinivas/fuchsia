// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"

TEST(Client, ServerReset) {
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

  struct EventHandler : public fdf::WireAsyncEventHandler<test_transport::EmptyProtocol> {
    void on_fidl_error(fidl::UnbindInfo unbind_info) override {
      EXPECT_TRUE(unbind_info.is_peer_closed());
      sync_completion_signal(&completion);
    }

    sync_completion_t completion;
  };

  EventHandler event_handler;
  fdf::WireSharedClient<test_transport::EmptyProtocol> client;
  client.Bind(std::move(client_end), dispatcher->get(), &event_handler);

  server_end.reset();

  sync_completion_wait(&event_handler.completion, ZX_TIME_INFINITE);

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

TEST(Client, ServerResetMidCall) {
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

  struct EventHandler : public fdf::WireAsyncEventHandler<test_transport::TwoWayTest> {
    void on_fidl_error(fidl::UnbindInfo unbind_info) override {
      EXPECT_TRUE(unbind_info.is_peer_closed());
      sync_completion_signal(&completion);
    }

    sync_completion_t completion;
  };

  EventHandler event_handler;
  fdf::WireSharedClient<test_transport::TwoWayTest> client;
  client.Bind(std::move(client_end), dispatcher->get(), &event_handler);

  auto arena = fdf::Arena::Create(0, "");
  ASSERT_OK(arena.status_value());
  sync_completion_t call_completion;
  client.buffer(*arena)->TwoWay(0u).ThenExactlyOnce(
      [&call_completion](fdf::WireUnownedResult<::test_transport::TwoWayTest::TwoWay>& result) {
        EXPECT_TRUE(result.is_peer_closed());
        sync_completion_signal(&call_completion);
      });

  server_end.reset();

  sync_completion_wait(&event_handler.completion, ZX_TIME_INFINITE);
  sync_completion_wait(&call_completion, ZX_TIME_INFINITE);

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}
