// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"
#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

struct TestServer : public fdf::WireServer<test_transport::TwoWayEmptyArgsTest> {
  void TwoWayEmptyArgs(TwoWayEmptyArgsRequestView request, fdf::Arena& in_request_arena,
                       TwoWayEmptyArgsCompleter::Sync& completer) override {
    // Test using a different arena in the response.
    fdf::Arena response_arena('DIFF');
    completer.buffer(response_arena).Reply();
  }
};

TEST(DriverTransport, WireTwoWayEmptyArgAsyncShared) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::TwoWayEmptyArgsTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::TwoWayEmptyArgsTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(
      dispatcher->get(), std::move(server_end), server,
      fidl_driver_testing::FailTestOnServerError<::test_transport::TwoWayEmptyArgsTest>());

  fdf::WireSharedClient<test_transport::TwoWayEmptyArgsTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  fdf::Arena arena('ORIG');

  libsync::Completion done;
  client.buffer(arena)->TwoWayEmptyArgs().ThenExactlyOnce(
      [&done](
          fdf::WireUnownedResult<::test_transport::TwoWayEmptyArgsTest::TwoWayEmptyArgs>& result) {
        ASSERT_OK(result.status());
        done.Signal();
      });
  ASSERT_OK(done.Wait());

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

}  // namespace
