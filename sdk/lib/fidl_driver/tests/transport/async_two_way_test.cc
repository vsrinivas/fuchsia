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

#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

constexpr uint32_t kRequestPayload = 1234;
constexpr uint32_t kResponsePayload = 5678;

struct TestServer : public fdf::WireServer<test_transport::TwoWayTest> {
  void TwoWay(TwoWayRequestView request, fdf::Arena& in_request_arena,
              TwoWayCompleter::Sync& completer) override {
    ASSERT_EQ(kRequestPayload, request->payload);
    ASSERT_EQ(fdf_request_arena, in_request_arena.get());

    // Test using a different arena in the response.
    auto response_arena = fdf::Arena::Create(0, "");
    fdf_response_arena = response_arena->get();
    completer.buffer(*response_arena).Reply(kResponsePayload);
  }

  fdf_arena_t* fdf_request_arena;
  fdf_arena_t* fdf_response_arena;
};

TEST(DriverTransport, TwoWayAsync) {
  void* driver = reinterpret_cast<void*>(uintptr_t(1));
  fdf_internal_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_internal_pop_driver(); });

  auto dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED);
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server,
                  fidl_driver_testing::FailTestOnServerError<::test_transport::TwoWayTest>());

  fdf::WireSharedClient<test_transport::TwoWayTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  auto arena = fdf::Arena::Create(0, "");
  ASSERT_OK(arena.status_value());
  server->fdf_request_arena = arena->get();
  sync_completion_t done;
  client.buffer(*arena)->TwoWay(
      kRequestPayload,
      [&done, &server](fdf::WireUnownedResult<::test_transport::TwoWayTest::TwoWay>& result) {
        ASSERT_OK(result.status());
        ASSERT_EQ(kResponsePayload, result->payload);
        ASSERT_EQ(server->fdf_response_arena, result.arena().get());
        sync_completion_signal(&done);
      });

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

}  // namespace
