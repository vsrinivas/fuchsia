// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.transport/cpp/driver/wire.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/assert_peer_closed_helper.h"
#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"
#include "sdk/lib/fidl_driver/tests/transport/server_on_unbound_helper.h"

namespace {

class TestServer : public fdf::WireServer<test_transport::SendZirconHandleTest> {
  void SendZirconHandle(SendZirconHandleRequestView request, fdf::Arena& arena,
                        SendZirconHandleCompleter::Sync& completer) override {
    completer.buffer(arena).Reply(std::move(request->h));
  }
};

class CreateNewHandlesTestServer : public fdf::WireServer<test_transport::SendZirconHandleTest> {
  void SendZirconHandle(SendZirconHandleRequestView request, fdf::Arena& arena,
                        SendZirconHandleCompleter::Sync& completer) override {
    ASSERT_TRUE(request->h.is_valid());

    zx::event ev;
    ASSERT_OK(zx::event::create(0, &ev));
    completer.buffer(arena).Reply(std::move(ev));
  }
};

template <typename TestServerType, bool HandlesEqual>
void TestImpl() {
  fidl_driver_testing::ScopedFakeDriver driver;

  libsync::Completion dispatcher_shutdown;
  auto dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fdf::ServerEnd<test_transport::SendZirconHandleTest> server_end(std::move(channels->end0));
  fdf::ClientEnd<test_transport::SendZirconHandleTest> client_end(std::move(channels->end1));

  auto server = std::make_shared<TestServer>();
  fdf::BindServer(
      dispatcher->get(), std::move(server_end), server,
      fidl_driver_testing::FailTestOnServerError<test_transport::SendZirconHandleTest>());

  fdf::WireSharedClient<test_transport::SendZirconHandleTest> client;
  client.Bind(std::move(client_end), dispatcher->get());
  sync_completion_t done;

  // Use a scope block to close the local arena reference after the message is sent.
  // This ensures that handles stored in the arena are closed before the arena is destructed.
  {
    fdf::Arena arena('TEST');

    zx::event ev;
    zx::event::create(0, &ev);
    zx_handle_t handle = ev.get();

    client.buffer(arena)
        ->SendZirconHandle(std::move(ev))
        .ThenExactlyOnce(
            [&done, handle](
                fdf::WireUnownedResult<::test_transport::SendZirconHandleTest::SendZirconHandle>&
                    result) {
              ASSERT_OK(result.status());
              ASSERT_TRUE(result->h.is_valid());
              if (HandlesEqual) {
                ASSERT_EQ(handle, result->h.get());
              }
              sync_completion_signal(&done);
            });
  }

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

TEST(DriverTransport, WireSendZirconHandleAsync) { TestImpl<TestServer, true>(); }

// Instead of echoing the handles, create new handles.
// This ensures CloseHandles() is called on the request object before the fdf::Arena
// is destructed. In the case of echo, the handle is moved out of the request object
// so this corner case is never encountered. (See fxbug.dev/102974 for motivation).
TEST(DriverTransport, WireSendNewZirconHandleAsync) {
  TestImpl<CreateNewHandlesTestServer, false>();
}

TEST(DriverTransport, WireSendZirconHandleEncodeErrorShouldCloseHandle) {
  fidl_driver_testing::ScopedFakeDriver driver;
  libsync::Completion dispatcher_shutdown;
  zx::result dispatcher =
      fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED, "",
                              [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown.Signal(); });
  ASSERT_OK(dispatcher.status_value());
  zx::result endpoints = fdf::CreateEndpoints<test_transport::OnErrorCloseHandlesTest>();
  ASSERT_OK(endpoints.status_value());
  fdf::Arena arena('TEST');

  fdf::WireSharedClient client(std::move(endpoints->client), dispatcher->get());

  zx::channel c1, c2;
  ASSERT_OK(zx::channel::create(0, &c1, &c2));

  fidl::Status status = client.buffer(arena)->SendZirconHandle("too long", std::move(c1));
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(fidl::Reason::kEncodeError, status.reason());
  ASSERT_NO_FAILURES(fidl_driver_testing::AssertPeerClosed(c2));

  dispatcher->ShutdownAsync();
  ASSERT_OK(dispatcher_shutdown.Wait());
}

}  // namespace
