// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

constexpr uint32_t kRequestPayload = 1234;
constexpr uint32_t kResponsePayload = 5678;

struct TestServer : public fdf::WireServer<test_transport::TwoWayTest> {
 public:
  explicit TestServer(sync::Completion* destroyed) : destroyed_(destroyed) {}
  ~TestServer() override { destroyed_->Signal(); }

  void TwoWay(TwoWayRequestView request, fdf::Arena& in_request_arena,
              TwoWayCompleter::Sync& completer) override {
    ZX_ASSERT(request->payload == kRequestPayload);
    ASSERT_EQ(fdf_request_arena, in_request_arena.get());

    // Test using a different arena in the response.
    auto response_arena = fdf::Arena::Create(0, "");
    fdf_response_arena = response_arena->get();
    completer.buffer(*response_arena).Reply(kResponsePayload);
  }

  fdf_arena_t* fdf_request_arena;
  fdf_arena_t* fdf_response_arena;

 private:
  sync::Completion* destroyed_;
};

TEST(DriverTransport, TwoWaySync) {
  void* driver = reinterpret_cast<void*>(uintptr_t(1));
  fdf_internal_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_internal_pop_driver(); });

  auto client_dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS);
  ASSERT_OK(client_dispatcher.status_value());

  auto server_dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS);
  ASSERT_OK(server_dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayTest> client_end(std::move(channels->end1));

  sync::Completion server_destruction;
  auto server = std::make_shared<TestServer>(&server_destruction);
  fdf::ServerBindingRef binding_ref =
      fdf::BindServer(server_dispatcher->get(), std::move(server_end), server,
                      fidl_driver_testing::FailTestOnServerError<test_transport::TwoWayTest>());
  zx::status<fdf::Arena> arena = fdf::Arena::Create(0, "");
  ASSERT_OK(arena.status_value());
  server->fdf_request_arena = arena->get();

  auto run_on_dispatcher_thread = [&] {
    fdf::WireSyncClient<test_transport::TwoWayTest> client(std::move(client_end));
    fdf::WireUnownedResult<test_transport::TwoWayTest::TwoWay> result =
        client.buffer(*arena)->TwoWay(kRequestPayload);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(kResponsePayload, result->payload);
    ASSERT_EQ(server->fdf_response_arena, result.arena().get());

    // TODO(fxbug.dev/92489): If this call and wait is removed, the test will
    // flake by leaking |AsyncServerBinding| objects.
    binding_ref.Unbind();
    server.reset();
  };
  async::PostTask(client_dispatcher->async_dispatcher(), run_on_dispatcher_thread);
  ASSERT_OK(server_destruction.Wait());
}

}  // namespace
