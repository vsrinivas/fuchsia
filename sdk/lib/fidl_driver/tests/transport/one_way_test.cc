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

#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

constexpr uint32_t kRequestPayload = 1234;

struct TestServer : public fdf::WireServer<test_transport::OneWayTest> {
  void OneWay(OneWayRequestView request, fdf::Arena arena,
              OneWayCompleter::Sync& completer) override {
    ASSERT_EQ(kRequestPayload, request->payload);
    ASSERT_EQ(fdf_request_arena, arena.get());

    sync_completion_signal(&done);
  }

  sync_completion_t done;
  fdf_arena_t* fdf_request_arena;
};

// TODO(fxbug.dev/92488): Investigate use-after-free in |driver_runtime::Dispatcher|.
TEST(DriverTransport, DISABLED_OneWay) {
  void* driver = reinterpret_cast<void*>(uintptr_t(1));
  fdf_internal_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_internal_pop_driver(); });

  auto dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED);
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
  auto arena = fdf::Arena::Create(0, "");
  ASSERT_OK(arena.status_value());
  server->fdf_request_arena = arena->get();
  // TODO(fxbug.dev/91107): Consider taking |const fdf::Arena&| or similar.
  // The arena is consumed after a single call.
  client.buffer(std::move(*arena))->OneWay(kRequestPayload);

  ASSERT_OK(sync_completion_wait(&server->done, ZX_TIME_INFINITE));
}

}  // namespace
