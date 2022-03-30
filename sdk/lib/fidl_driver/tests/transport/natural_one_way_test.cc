// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/fidl.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"
#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

std::vector<uint8_t> kRequestPayload = {1, 2, 3, 4};

struct TestServer : public fdf::WireServer<test_transport::OneWayTest> {
  void OneWay(OneWayRequestView request, fdf::Arena& arena,
              OneWayCompleter::Sync& completer) override {
    ASSERT_EQ(kRequestPayload.size(), request->payload.count());
    ASSERT_BYTES_EQ(kRequestPayload.data(), request->payload.data(), kRequestPayload.size());

    sync_completion_signal(&done);
  }

  sync_completion_t done;
};

TEST(DriverTransport, NaturalOneWayVector) {
  fidl_driver_testing::ScopedFakeDriver driver;

  auto dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED);
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::OneWayTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::OneWayTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(dispatcher->get(), std::move(server_end), server,
                  fidl_driver_testing::FailTestOnServerError<test_transport::OneWayTest>());

  fdf::SharedClient<test_transport::OneWayTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  auto result = client->OneWay(kRequestPayload);
  ZX_ASSERT(result.is_ok());

  ASSERT_OK(sync_completion_wait(&server->done, ZX_TIME_INFINITE));
}

}  // namespace
