// Copyright 2022 The Fuchsia Authors. All rights reserved.
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
TEST(DriverTransport, DISABLED_TwoWaySync) {
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
  zx::status<fdf::Arena> arena = fdf::Arena::Create(0, "");
  fdf::WireSyncClient<test_transport::TransportTest> client(std::move(client_end));
  fdf::WireUnownedResult<test_transport::TransportTest::TwoWay> result =
      client.buffer(std::move(*arena))->TwoWay(kRequestPayload);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(kResponsePayload, result->payload);
}
