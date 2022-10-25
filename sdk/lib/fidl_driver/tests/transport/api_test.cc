// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/sync/cpp/completion.h>

#include <gtest/gtest.h>

#include "sdk/lib/fidl_driver/tests/transport/api_test_helper.h"
#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"
#include "src/lib/testing/predicates/status.h"

// Test creating a typed channel endpoint pair.
TEST(Endpoints, CreateFromProtocol) {
  // `std::move` pattern
  {
    auto endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_EQ(ZX_OK, endpoints.status_value());
    fdf::ClientEnd<test_transport::TwoWayTest> client_end = std::move(endpoints->client);
    fdf::ServerEnd<test_transport::TwoWayTest> server_end = std::move(endpoints->server);

    ASSERT_TRUE(client_end.is_valid());
    ASSERT_TRUE(server_end.is_valid());
  }

  // Destructuring pattern
  {
    auto endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_EQ(ZX_OK, endpoints.status_value());
    auto [client_end, server_end] = std::move(endpoints.value());

    ASSERT_TRUE(client_end.is_valid());
    ASSERT_TRUE(server_end.is_valid());
  }
}

// Test creating a typed channel endpoint pair using the out-parameter
// overloads.
TEST(Endpoints, CreateFromProtocolOutParameterStyleClientRetained) {
  fdf::ClientEnd<test_transport::TwoWayTest> client_end;
  auto server_end = fdf::CreateEndpoints(&client_end);
  ASSERT_OK(server_end.status_value());
  ASSERT_EQ(ZX_OK, server_end.status_value());

  ASSERT_TRUE(client_end.is_valid());
  ASSERT_TRUE(server_end->is_valid());
}

TEST(Endpoints, CreateFromProtocolOutParameterStyleServerRetained) {
  fdf::ServerEnd<test_transport::TwoWayTest> server_end;
  auto client_end = fdf::CreateEndpoints(&server_end);
  ASSERT_OK(client_end.status_value());
  ASSERT_EQ(ZX_OK, client_end.status_value());

  ASSERT_TRUE(server_end.is_valid());
  ASSERT_TRUE(client_end->is_valid());
}

TEST(WireClient, CannotDestroyInDifferentDispatcherThanBound) {
  DEBUG_ONLY_TEST_MAY_SKIP();
  fidl_driver_testing::ScopedFakeDriver driver;

  auto [dispatcher1, dispatcher1_shutdown] = CreateSyncDispatcher();
  auto [dispatcher2, dispatcher2_shutdown] = CreateSyncDispatcher();
  zx::result endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
  ASSERT_OK(endpoints.status_value());

  std::unique_ptr<fdf::WireClient<test_transport::TwoWayTest>> client;

  // Create on one.
  libsync::Completion created;
  async::PostTask(dispatcher1.async_dispatcher(), [&, dispatcher = dispatcher1.get()] {
    client = std::make_unique<fdf::WireClient<test_transport::TwoWayTest>>();
    client->Bind(std::move(endpoints->client), dispatcher);
    created.Signal();
  });
  ASSERT_OK(created.Wait());

  // Destroy on another.
  libsync::Completion destroyed;
  async::PostTask(dispatcher2.async_dispatcher(), [&] {
    ASSERT_DEATH(client.reset(),
                 "The selected FIDL bindings is thread unsafe. Access from multiple driver "
                 "dispatchers detected. This is not allowed. Ensure the object is used from the "
                 "same |fdf_dispatcher_t|.");
    destroyed.Signal();
  });
  ASSERT_OK(destroyed.Wait());

  dispatcher1.ShutdownAsync();
  dispatcher2.ShutdownAsync();

  ASSERT_OK(dispatcher1_shutdown->Wait());
  ASSERT_OK(dispatcher2_shutdown->Wait());
}

TEST(WireClient, CannotDestroyOnUnmanagedThread) {
  DEBUG_ONLY_TEST_MAY_SKIP();
  fidl_driver_testing::ScopedFakeDriver driver;

  auto [dispatcher1, dispatcher1_shutdown] = CreateSyncDispatcher();
  zx::result endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
  ASSERT_OK(endpoints.status_value());

  std::unique_ptr<fdf::WireClient<test_transport::TwoWayTest>> client;

  // Create on one.
  libsync::Completion created;
  async::PostTask(dispatcher1.async_dispatcher(), [&, dispatcher = dispatcher1.get()] {
    client = std::make_unique<fdf::WireClient<test_transport::TwoWayTest>>();
    client->Bind(std::move(endpoints->client), dispatcher);
    created.Signal();
  });
  ASSERT_OK(created.Wait());

  // Destroy on another.
  libsync::Completion destroyed;
  std::thread thread([&] {
    ASSERT_DEATH(
        client.reset(),
        "The selected FIDL bindings is thread unsafe. The current thread is not managed by a "
        "driver dispatcher. Ensure the object is always used from a dispatcher managed thread.");
    destroyed.Signal();
  });
  ASSERT_OK(destroyed.Wait());
  thread.join();

  dispatcher1.ShutdownAsync();
  ASSERT_OK(dispatcher1_shutdown->Wait());
}

using DispatcherOptions = uint32_t;

// Test the shared client using both synchronized and unsynchronized dispatcher.
class WireSharedClient : public testing::TestWithParam<DispatcherOptions> {};

TEST_P(WireSharedClient, CanSendAcrossDispatcher) {
  fidl_driver_testing::ScopedFakeDriver driver;

  auto [dispatcher1, dispatcher1_shutdown] = CreateSyncDispatcher();
  auto [dispatcher2, dispatcher2_shutdown] = CreateSyncDispatcher();
  zx::result endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
  ASSERT_OK(endpoints.status_value());

  std::unique_ptr<fdf::WireSharedClient<test_transport::TwoWayTest>> client;

  // Create on one.
  libsync::Completion created;
  async::PostTask(dispatcher1.async_dispatcher(), [&, dispatcher = dispatcher1.get()] {
    client = std::make_unique<fdf::WireSharedClient<test_transport::TwoWayTest>>();
    client->Bind(std::move(endpoints->client), dispatcher);
    created.Signal();
  });
  ASSERT_OK(created.Wait());

  // Destroy on another.
  libsync::Completion destroyed;
  async::PostTask(dispatcher2.async_dispatcher(), [&] {
    client.reset();
    destroyed.Signal();
  });
  ASSERT_OK(destroyed.Wait());

  dispatcher1.ShutdownAsync();
  dispatcher2.ShutdownAsync();
  ASSERT_OK(dispatcher1_shutdown->Wait());
  ASSERT_OK(dispatcher2_shutdown->Wait());
}

TEST_P(WireSharedClient, CanDestroyOnUnmanagedThread) {
  fidl_driver_testing::ScopedFakeDriver driver;

  auto [dispatcher1, dispatcher1_shutdown] = CreateSyncDispatcher();
  zx::result endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
  ASSERT_OK(endpoints.status_value());

  std::unique_ptr<fdf::WireSharedClient<test_transport::TwoWayTest>> client;

  // Create on one.
  libsync::Completion created;
  libsync::Completion destroyed;
  async::PostTask(dispatcher1.async_dispatcher(), [&, dispatcher = dispatcher1.get()] {
    client = std::make_unique<fdf::WireSharedClient<test_transport::TwoWayTest>>();
    client->Bind(std::move(endpoints->client), dispatcher,
                 fidl::ObserveTeardown([&] { destroyed.Signal(); }));
    created.Signal();
  });
  ASSERT_OK(created.Wait());

  // Destroy on another.
  std::thread thread([&] { client.reset(); });
  ASSERT_OK(destroyed.Wait());
  thread.join();

  dispatcher1.ShutdownAsync();
  ASSERT_OK(dispatcher1_shutdown->Wait());
}

TEST(WireClient, CannotBindUnsynchronizedDispatcher) {
  DEBUG_ONLY_TEST_MAY_SKIP();
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  zx::result dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  zx::result endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
  ASSERT_OK(endpoints.status_value());

  fdf::WireClient<test_transport::TwoWayTest> client;
  libsync::Completion created;
  async::PostTask(dispatcher->async_dispatcher(), [&] {
    ASSERT_DEATH(client.Bind(std::move(endpoints->client), dispatcher->get()),
                 "The selected FIDL bindings is thread unsafe. A synchronized fdf_dispatcher_t is "
                 "required. Ensure the fdf_dispatcher_t does not have the "
                 "|FDF_DISPATCHER_OPTION_UNSYNCHRONIZED| option.");
    client = {};
    created.Signal();
  });
  ASSERT_OK(created.Wait());

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

TEST_P(WireSharedClient, CanBindAnyDispatcher) {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  zx::result dispatcher = fdf::Dispatcher::Create(
      GetParam(), "", [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  zx::result endpoints = fdf::CreateEndpoints<test_transport::TwoWayTest>();
  ASSERT_OK(endpoints.status_value());

  fdf::WireSharedClient<test_transport::TwoWayTest> client;
  libsync::Completion created;
  async::PostTask(dispatcher->async_dispatcher(), [&] {
    client.Bind(std::move(endpoints->client), dispatcher->get());
    client = {};
    created.Signal();
  });
  ASSERT_OK(created.Wait());

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

INSTANTIATE_TEST_SUITE_P(WireSharedClientTests, WireSharedClient,
                         testing::Values<DispatcherOptions>(FDF_DISPATCHER_OPTION_SYNCHRONIZED,
                                                            FDF_DISPATCHER_OPTION_UNSYNCHRONIZED),
                         [](const auto info) {
                           switch (info.index) {
                             case 0:
                               return "Synchronized";
                             case 1:
                               return "Unsynchronized";
                             default:
                               ZX_PANIC("Invalid index");
                           }
                         });
