// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

constexpr uint32_t kRequestPayload = 1234;
constexpr uint32_t kResponsePayload = 5678;

class TestServer : public fdf::WireServer<test_transport::TransportTest> {
  void TwoWay(TwoWayRequestView request, fdf::Arena arena,
              TwoWayCompleter::Sync& completer) override {
    ZX_ASSERT(request->payload == kRequestPayload);
    completer.Reply(kResponsePayload, std::move(arena));
  }
};

// TODO(fxbug.dev/87387) Enable this test once fdf::ChannelRead supports Cancel().
TEST(DriverTransport, DISABLED_TwoWayAsync) {
  void* driver = reinterpret_cast<void*>(uintptr_t(1));
  fdf_internal_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_internal_pop_driver(); });

  auto dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED);
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TransportTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TransportTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server);

  fdf::WireSharedClient<test_transport::TransportTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  auto arena = fdf::Arena::Create(0, "");
  ASSERT_OK(arena.status_value());
  sync_completion_t done;
  // TODO(fxbug.dev/91107): Consider taking |const fdf::Arena&| or similar.
  // The arena is consumed after a single call.
  client.buffer(std::move(*arena))
      ->TwoWay(kRequestPayload,
               [&done](fdf::WireUnownedResult<::test_transport::TransportTest::TwoWay>& result) {
                 ASSERT_OK(result.status());
                 ASSERT_EQ(kResponsePayload, result->payload);
                 sync_completion_signal(&done);
               });

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}
