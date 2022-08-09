// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
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

// The payload will be sent as a vector.
// This has 10 elements because the iovec implementation has special handling
// on the last aligned 8-byte region due to padding. At least in the current
// implementation, this forces an extra iovec to be produced if there is
// sufficient space in the iovec buffer. This tests driver behavior in this
// case.
std::array<uint8_t, 10> kRequestPayload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

struct TestServer : public fdf::WireServer<test_transport::OneWayTest> {
  void OneWay(OneWayRequestView request, fdf::Arena& arena,
              OneWayCompleter::Sync& completer) override {
    ASSERT_EQ(kRequestPayload.size(), request->payload.count());
    ASSERT_BYTES_EQ(kRequestPayload.data(), request->payload.data(), kRequestPayload.size());
    ASSERT_EQ(fdf_request_arena, arena.get());

    sync_completion_signal(&done);
  }

  sync_completion_t done;
  fdf_arena_t* fdf_request_arena;
};

TEST(DriverTransport, WireOneWayVector) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::OneWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::OneWayTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server,
                  fidl_driver_testing::FailTestOnServerError<test_transport::OneWayTest>());

  fdf::WireSharedClient<test_transport::OneWayTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  auto arena = fdf::Arena::Create(0, 'TEST');
  ASSERT_OK(arena.status_value());
  server->fdf_request_arena = arena->get();
  auto result =
      client.buffer(*arena)->OneWay(fidl::VectorView<uint8_t>::FromExternal(kRequestPayload));
  ZX_ASSERT(result.ok());

  ASSERT_OK(sync_completion_wait(&server->done, ZX_TIME_INFINITE));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

TEST(DriverTransport, WireOneWayVectorSyncViaAsyncClient) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::OneWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::OneWayTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server,
                  fidl_driver_testing::FailTestOnServerError<test_transport::OneWayTest>());

  fdf::WireSharedClient<test_transport::OneWayTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  auto arena = fdf::Arena::Create(0, 'TEST');
  ASSERT_OK(arena.status_value());
  server->fdf_request_arena = arena->get();
  auto result = client.sync().buffer(*arena)->OneWay(
      fidl::VectorView<uint8_t>::FromExternal(kRequestPayload));
  ASSERT_OK(result.status());

  ASSERT_OK(sync_completion_wait(&server->done, ZX_TIME_INFINITE));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

}  // namespace
