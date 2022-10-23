// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <algorithm>
#include <iterator>

#include <zxtest/zxtest.h>

#include "pty-server.h"

namespace {

using Device = fuchsia_hardware_pty::Device;
using Connection = fidl::WireSyncClient<Device>;

class PtyTestCase : public zxtest::Test {
 public:
  PtyTestCase() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    zx::result args = PtyServer::Args::Create();
    ASSERT_OK(args.status_value());
    std::shared_ptr server =
        std::make_shared<PtyServer>(std::move(args.value()), loop_.dispatcher());
    zx::result endpoints = fidl::CreateEndpoints<Device>();
    ASSERT_OK(endpoints.status_value());
    server->AddConnection(std::move(endpoints->server));

    ASSERT_OK(loop_.StartThread("pty-test"));

    server_ = Connection(std::move(endpoints->client));
  }

 protected:
  static zx::result<Connection> OpenClient(Connection& conn, uint32_t id) {
    auto endpoints = fidl::CreateEndpoints<Device>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    fidl::WireResult result = conn->OpenClient(id, std::move(endpoints->server));
    if (result.status() != ZX_OK) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    if (result->s != ZX_OK) {
      return zx::error(result->s);
    }
    return zx::ok(Connection(std::move(endpoints->client)));
  }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  Connection take_server() { return std::move(server_); }

 private:
  async::Loop loop_;
  Connection server_;
};

zx::eventpair GetEvent(Connection& conn) {
  auto result = conn->Describe2();
  if (result.status() != ZX_OK) {
    return {};
  }
  if (!result.value().has_event()) {
    return {};
  }
  return std::move(result.value().event());
}

void WriteCtrlC(Connection& conn) {
  uint8_t data[] = {0x03};
  const fidl::WireResult result = conn->Write(fidl::VectorView<uint8_t>::FromExternal(data));
  ASSERT_OK(result.status());
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  ASSERT_EQ(response.value()->actual_count, std::size(data));
}

// Make sure the server connections describe appropriately
TEST_F(PtyTestCase, ServerDescribe) {
  Connection server{take_server()};
  auto result = server->Describe2();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().has_event());
  ASSERT_TRUE(result.value().event().is_valid());
}  // namespace

TEST_F(PtyTestCase, ServerSetWindowSize) {
  Connection server{take_server()};
  auto result = server->SetWindowSize({.width = 80, .height = 24});
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
}  // namespace

TEST_F(PtyTestCase, ServerClrSetFeature) {
  Connection server{take_server()};
  auto result = server->ClrSetFeature(0, 0);
  ASSERT_OK(result.status());
  // ClrSetFeature is only meaningful on clients
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PtyTestCase, ServerGetWindowSize) {
  Connection server{take_server()};
  auto result = server->GetWindowSize();
  ASSERT_OK(result.status());
  // Our original implementation didn't support this, so preserve that behavior.
  // It's not clear why, though.  If this is causing problems, we should
  // probably just implement it.
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PtyTestCase, ServerMakeActive) {
  Connection server{take_server()};
  auto result = server->MakeActive(0);
  ASSERT_OK(result.status());
  // MakeActive is only meaningful on clients
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PtyTestCase, ServerReadEvents) {
  Connection server{take_server()};
  auto result = server->ReadEvents();
  ASSERT_OK(result.status());
  // ReadEvents is only meaningful on clients
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

// Basic test of opening a client
TEST_F(PtyTestCase, ServerBasicOpenClient) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  // Make sure our client connection is valid after this
  ASSERT_STATUS(
      client.value().client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Try opening two clients with the same id
TEST_F(PtyTestCase, ServerOpenClientTwice) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());
  ASSERT_STATUS(OpenClient(server, 0).status_value(), ZX_ERR_INVALID_ARGS);

  // Our original client connection should still be good.
  ASSERT_STATUS(
      client.value().client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Try opening two clients with different ids
TEST_F(PtyTestCase, ServerOpenClientTwoDifferent) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());
  zx::result client2 = OpenClient(server, 0);
  ASSERT_OK(client2.status_value());

  // Both connections should be good
  ASSERT_STATUS(
      client.value().client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
  ASSERT_STATUS(
      client2.value().client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Verify a server with no clients behaves as expected
TEST_F(PtyTestCase, ServerWithNoClientsInitialConditions) {
  Connection server{take_server()};
  zx::eventpair event = GetEvent(server);

  auto check_state = [&]() {
    zx_signals_t observed = 0;
    ASSERT_STATUS(event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
    // Precisely this set of signals should be asserted
    ASSERT_EQ(observed, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable |
                                                  fuchsia_device::wire::DeviceSignal::kHangup));

    // Attempts to read should get 0 bytes and ZX_OK
    {
      const fidl::WireResult result = server->Read(10);
      ASSERT_OK(result.status());
      const fit::result response = result.value();
      ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
      const fidl::VectorView data = response.value()->data;
      ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
                std::string_view());
    }

    // Attempts to write should fail with ZX_ERR_PEER_CLOSED
    {
      uint8_t data[16] = {};
      const fidl::WireResult result = server->Write(fidl::VectorView<uint8_t>::FromExternal(data));
      ASSERT_OK(result.status());
      const fit::result response = result.value();
      ASSERT_TRUE(response.is_error());
      ASSERT_STATUS(response.error_value(), ZX_ERR_PEER_CLOSED);
    }
  };

  ASSERT_NO_FATAL_FAILURE(check_state());

  // Create a client and close it, then make sure we're back in the initial
  // state
  {
    zx::result client = OpenClient(server, 1);
    ASSERT_OK(client.status_value());
  }
  // Wait for the server to signal that it got the client disconnect
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kHangup),
                           zx::time::infinite(), nullptr));

  ASSERT_NO_FATAL_FAILURE(check_state());
}

// Verify a server with a client has the right state
TEST_F(PtyTestCase, ServerWithClientInitialConditions) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  zx::eventpair server_event = GetEvent(server);
  zx::eventpair client_event = GetEvent(client.value());

  zx_signals_t observed = 0;
  ASSERT_STATUS(server_event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
  ASSERT_EQ(observed, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable));

  observed = 0;
  ASSERT_STATUS(client_event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
  ASSERT_EQ(observed, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable));

  // Attempts to read on either side should get SHOULD_WAIT
  {
    const fidl::WireResult result = server->Read(10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_error());
    ASSERT_STATUS(response.error_value(), ZX_ERR_SHOULD_WAIT);
  }
  {
    const fidl::WireResult result = client->Read(10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_error());
    ASSERT_STATUS(response.error_value(), ZX_ERR_SHOULD_WAIT);
  }

  // Client should be in cooked mode
  {
    auto result = client->ClrSetFeature(0, 0);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, 0);
  }
}

// Verify a read from a server for 0 bytes doesn't return ZX_ERR_SHOULD_WAIT
TEST_F(PtyTestCase, ServerEmpty0ByteRead) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  const fidl::WireResult result = server->Read(0);
  ASSERT_OK(result.status());
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  const fidl::VectorView data = response.value()->data;
  ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
            std::string_view());
}

// Verify a write by the server for 0 bytes when the receiving client is full doesn't return
// ZX_ERR_SHOULD_WAIT
TEST_F(PtyTestCase, ClientFull0ByteServerWrite) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  // Fill up FIFO
  while (true) {
    uint8_t buf[256] = {};
    const fidl::WireResult result = server->Write(fidl::VectorView<uint8_t>::FromExternal(buf));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    if (response.is_error()) {
      ASSERT_STATUS(response.error_value(), ZX_ERR_SHOULD_WAIT);
      break;
    }
    ASSERT_GT(response.value()->actual_count, 0);
  }

  const fidl::WireResult result = server->Write({});
  ASSERT_OK(result.status());
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  ASSERT_EQ(response.value()->actual_count, 0);
}

// Verify a write by a client for 0 bytes when the client isn't active returns
// ZX_ERR_SHOULD_WAIT
TEST_F(PtyTestCase, ClientInactive0ByteClientWrite) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());
  zx::result inactive_client = OpenClient(server, 0);
  ASSERT_OK(inactive_client.status_value());

  const fidl::WireResult result = inactive_client->Write({});
  ASSERT_OK(result.status());
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_error());
  ASSERT_STATUS(response.error_value(), ZX_ERR_SHOULD_WAIT);
}

// Make sure the client connections describe appropriately
TEST_F(PtyTestCase, ClientDescribe) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  auto result = client->Describe2();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().has_event());
  ASSERT_TRUE(result.value().event().is_valid());
}

TEST_F(PtyTestCase, ClientWindowSize) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  {
    auto result = server->SetWindowSize({.width = 80, .height = 24});
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
  {
    auto result = client->GetWindowSize();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->size.width, 80);
    ASSERT_EQ(result->size.height, 24);
  }
  {
    auto result = client->SetWindowSize({.width = 5, .height = 32});
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
  {
    auto result = client->GetWindowSize();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->size.width, 5);
    ASSERT_EQ(result->size.height, 32);
  }
}

TEST_F(PtyTestCase, ClientClrSetFeature) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  {
    auto result = client->ClrSetFeature(0, 0);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, 0);
  }

  // Make sure we can set bits
  {
    auto result = client->ClrSetFeature(0, fuchsia_hardware_pty::wire::kFeatureRaw);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, fuchsia_hardware_pty::wire::kFeatureRaw);
  }

  // If we don't change any bits, we should see the new settings
  {
    auto result = client->ClrSetFeature(0, 0);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, fuchsia_hardware_pty::wire::kFeatureRaw);
  }

  // Make sure we can clear bits
  {
    auto result = client->ClrSetFeature(fuchsia_hardware_pty::wire::kFeatureRaw, 0);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, 0);
  }
}

TEST_F(PtyTestCase, ClientClrSetFeatureInvalidBit) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  {
    auto result = client->ClrSetFeature(0, 0x2);
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(result->features, 0);
  }

  {
    auto result = client->ClrSetFeature(0x2, 0);
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(result->features, 0);
  }
}

TEST_F(PtyTestCase, ClientGetWindowSizeServerNeverSet) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  auto result = client->GetWindowSize();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->size.width, 0);
  ASSERT_EQ(result->size.height, 0);
}

// Each client should have its own feature flags
TEST_F(PtyTestCase, ClientIndependentFeatureFlags) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());
  zx::result client2 = OpenClient(server, 0);
  ASSERT_OK(client2.status_value());

  {
    auto result = client->ClrSetFeature(0, fuchsia_hardware_pty::wire::kFeatureRaw);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, fuchsia_hardware_pty::wire::kFeatureRaw);
  }

  {
    // Client 2 shouldn't see the changes
    auto result = client2->ClrSetFeature(0, 0);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, 0);
  }
}

TEST_F(PtyTestCase, ClientMakeActive) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());
  zx::result client2 = OpenClient(server, 0);
  ASSERT_OK(client2.status_value());

  {
    auto result = client->MakeActive(0);
    ASSERT_OK(result.status());
    // This client is not the controlling client (id=0), so it cannot change the
    // active client
    ASSERT_STATUS(result->status, ZX_ERR_ACCESS_DENIED);
  }
  {
    auto result = client2->MakeActive(1);
    ASSERT_OK(result.status());
    // This client is the controlling client (id=0), so it can.
    ASSERT_OK(result->status);
  }
  {
    // Changing the active client to the existing active client should be fine
    auto result = client2->MakeActive(1);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
  {
    // Changing the active client to the control client should be fine
    auto result = client2->MakeActive(0);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
  {
    // Changing the active client to a non-existent client should fail
    auto result = client2->MakeActive(2);
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->status, ZX_ERR_NOT_FOUND);
  }
}

TEST_F(PtyTestCase, ClientReadEvents) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());
  zx::result client2 = OpenClient(server, 0);
  ASSERT_OK(client2.status_value());

  {
    auto result = client->ReadEvents();
    ASSERT_OK(result.status());
    // This client is not the controlling client (id=0), so it cannot read events
    ASSERT_STATUS(result->status, ZX_ERR_ACCESS_DENIED);
  }

  {
    auto result = client2->ReadEvents();
    ASSERT_OK(result.status());
    // This client is the controlling client (id=0), so it can read events
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, 0);
  }
}

// Reading events should clear the event condition
TEST_F(PtyTestCase, ClientReadEventsClears) {
  Connection server{take_server()};
  zx::result active_client = OpenClient(server, 1);
  ASSERT_OK(active_client.status_value());
  zx::result control_client = OpenClient(server, 0);
  ASSERT_OK(control_client.status_value());

  zx::eventpair control_event = GetEvent(control_client.value());

  // No events yet
  ASSERT_STATUS(
      control_event.wait_one(static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent),
                             zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);

  // Write a ^C byte from the server to trigger a cooked-mode event
  ASSERT_NO_FATAL_FAILURE(WriteCtrlC(server));

  ASSERT_OK(
      control_event.wait_one(static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent),
                             zx::time::infinite(), nullptr));

  {
    auto result = control_client->ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, fuchsia_hardware_pty::wire::kEventInterrupt);
  }

  // Signal should have cleared
  ASSERT_STATUS(
      control_event.wait_one(static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent),
                             zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);

  // Event should have cleared
  {
    auto result = control_client->ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, 0);
  }
}

// Events arrive even without a controlling client connected
TEST_F(PtyTestCase, EventsSentWithNoControllingClient) {
  Connection server{take_server()};
  zx::result active_client = OpenClient(server, 1);
  ASSERT_OK(active_client.status_value());

  // Write a ^C byte from the server to trigger a cooked-mode event
  ASSERT_NO_FATAL_FAILURE(WriteCtrlC(server));

  // Connect a control client to inspect the event
  zx::result control_client = OpenClient(server, 0);
  ASSERT_OK(control_client.status_value());

  zx::eventpair control_event = GetEvent(control_client.value());
  ASSERT_OK(control_event.wait_one(
      static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent), zx::time{}, nullptr));

  {
    auto result = control_client->ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, fuchsia_hardware_pty::wire::kEventInterrupt);
  }
}

TEST_F(PtyTestCase, SetWindowSizeSendsEvent) {
  Connection server{take_server()};
  zx::result control_client = OpenClient(server, 0);
  ASSERT_OK(control_client.status_value());

  zx::eventpair control_event = GetEvent(control_client.value());

  // No events yet
  ASSERT_STATUS(control_event.wait_one(static_cast<zx_signals_t>(static_cast<zx_signals_t>(
                                           fuchsia_hardware_pty::wire::kSignalEvent)),
                                       zx::time{}, nullptr),
                ZX_ERR_TIMED_OUT);
  {
    auto result = control_client->ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, 0);
  }

  // SetWindowSize which should trigger an event
  {
    auto result = server->SetWindowSize({.width = 123, .height = 45});
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  ASSERT_OK(
      control_event.wait_one(static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent),
                             zx::time::infinite(), nullptr));
  {
    auto result = control_client->ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, fuchsia_hardware_pty::wire::kEventWindowSize);
  }
}

TEST_F(PtyTestCase, NonControllingClientOpenClient) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  // This client is not the controlling client (id=0), so it cannot create new
  // clients
  ASSERT_STATUS(OpenClient(client.value(), 2).status_value(), ZX_ERR_ACCESS_DENIED);
}

TEST_F(PtyTestCase, ControllingClientOpenClient) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  zx::result client2 = OpenClient(client.value(), 1);
  ASSERT_OK(client2.status_value());
}

TEST_F(PtyTestCase, ActiveClientCloses) {
  Connection server{take_server()};
  zx::result control_client = OpenClient(server, 0);
  ASSERT_OK(control_client.status_value());
  {
    zx::result active_client = OpenClient(server, 1);
    ASSERT_OK(active_client.status_value());
    auto result = control_client->MakeActive(1);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  zx::eventpair control_event = GetEvent(control_client.value());
  zx_signals_t observed = 0;
  ASSERT_OK(
      control_event.wait_one(static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent),
                             zx::time::infinite(), &observed));
  // Wait again with no timeout, so that observed doesn't have any transient
  // signals in it.
  ASSERT_OK(
      control_event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kHangup),
                             zx::time{}, &observed));
  ASSERT_EQ(observed, static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent |
                                                fuchsia_device::wire::DeviceSignal::kHangup));

  auto result = control_client->ReadEvents();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->events, fuchsia_hardware_pty::wire::kEventHangup);
}

// Makes sure nothing goes wrong when the active client is the controling
// client and it closes.
TEST_F(PtyTestCase, ActiveClientClosesWhenControl) {
  Connection server{take_server()};
  {
    zx::result control_client = OpenClient(server, 0);
    ASSERT_OK(control_client.status_value());
  }
  zx::eventpair event = GetEvent(server);
  zx_signals_t observed = 0;
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kHangup),
                           zx::time::infinite(), &observed));
}

TEST_F(PtyTestCase, ServerClosesWhenClientPresent) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 0);
  ASSERT_OK(client.status_value());

  // Write some data to the client, so we can verify the client can drain the
  // buffer still.

  uint8_t kTestData[] = "hello world";
  {
    const fidl::WireResult result =
        server->Write(fidl::VectorView<uint8_t>::FromExternal(kTestData));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_EQ(response.value()->actual_count, std::size(kTestData));
  }

  server = {};

  zx::eventpair event = GetEvent(client.value());
  zx_signals_t observed = 0;
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kHangup),
                           zx::time::infinite(), &observed));
  // Wait again with no timeout, so that observed doesn't have any transient
  // signals in it.
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kHangup),
                           zx::time{}, &observed));
  ASSERT_EQ(observed, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kHangup |
                                                fuchsia_device::wire::DeviceSignal::kReadable));

  {
    auto result = client->ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, fuchsia_hardware_pty::wire::kEventHangup);
  }

  // Attempts to drain the buffer should succeed
  {
    // Request more bytes than are present
    const fidl::WireResult result = client->Read(std::size(kTestData) + 10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
              std::string_view(reinterpret_cast<char*>(kTestData), std::size(kTestData)));
  }

  // Attempts to read the empty buffer should fail with ZX_ERR_PEER_CLOSED
  {
    const fidl::WireResult result = client->Read(10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_error());
    ASSERT_STATUS(response.error_value(), ZX_ERR_PEER_CLOSED);
  }

  // Attempts to write should fail with ZX_ERR_PEER_CLOSED
  {
    uint8_t data[16] = {};
    const fidl::WireResult result = client->Write(fidl::VectorView<uint8_t>::FromExternal(data));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_error());
    ASSERT_STATUS(response.error_value(), ZX_ERR_PEER_CLOSED);
  }
}

// Test writes from the client to the server when the client is cooked
TEST_F(PtyTestCase, ServerReadClientCooked) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  // In cooked mode, client writes should have \n transformed to \r\n, and
  // control chars untouched.
  uint8_t kTestData[] = "hello\x03 world\ntest message\n";
  const uint8_t kExpectedReadback[] = "hello\x03 world\r\ntest message\r\n";
  {
    const fidl::WireResult result =
        client->Write(fidl::VectorView<uint8_t>::FromExternal(kTestData));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_EQ(response.value()->actual_count, std::size(kTestData));
  }

  zx::eventpair event = GetEvent(server);
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                           zx::time::infinite(), nullptr));
  {
    const fidl::WireResult result = server->Read(std::size(kExpectedReadback) + 10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
              std::string_view(reinterpret_cast<const char*>(kExpectedReadback),
                               std::size(kExpectedReadback)));
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                     zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Test writes from the server to the client when the client is cooked
TEST_F(PtyTestCase, ServerWriteClientCooked) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  // In cooked mode, server writes should have newlines untouched and control
  // chars should cause a short write
  uint8_t kTestData[] = "hello world\ntest\x03 message\n";
  // We expect to read this back, but without the trailing nul
  const uint8_t kExpectedReadbackWithNul[] = "hello world\ntest";
  {
    const fidl::WireResult result =
        server->Write(fidl::VectorView<uint8_t>::FromExternal(kTestData));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    // We expect to see the written count to include the ^C
    ASSERT_EQ(response.value()->actual_count, std::size(kExpectedReadbackWithNul) - 1 + 1);
  }

  zx::eventpair event = GetEvent(client.value());
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                           zx::time::infinite(), nullptr));
  {
    const fidl::WireResult result = client->Read(std::size(kExpectedReadbackWithNul) + 10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
              std::string_view(reinterpret_cast<const char*>(kExpectedReadbackWithNul),
                               std::size(kExpectedReadbackWithNul) - 1));
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                     zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Test writes from the client to the server when the client is raw
TEST_F(PtyTestCase, ServerReadClientRaw) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  {
    auto result = client->ClrSetFeature(0, fuchsia_hardware_pty::wire::kFeatureRaw);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  // In raw mode, client writes should be untouched.
  uint8_t kTestData[] = "hello\x03 world\ntest message\n";
  {
    const fidl::WireResult result =
        client->Write(fidl::VectorView<uint8_t>::FromExternal(kTestData));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_EQ(response.value()->actual_count, std::size(kTestData));
  }

  zx::eventpair event = GetEvent(server);
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                           zx::time::infinite(), nullptr));
  {
    const fidl::WireResult result = server->Read(std::size(kTestData) + 10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
              std::string_view(reinterpret_cast<char*>(kTestData), std::size(kTestData)));
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                     zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Test writes from the server to the client when the client is raw
TEST_F(PtyTestCase, ServerWriteClientRaw) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());
  zx::result control_client = OpenClient(server, 0);
  ASSERT_OK(control_client.status_value());

  {
    auto result = client->ClrSetFeature(0, fuchsia_hardware_pty::wire::kFeatureRaw);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  // In raw mode, server writes should be untouched.
  uint8_t kTestData[] = "hello world\ntest\x03 message\n";
  {
    const fidl::WireResult result =
        server->Write(fidl::VectorView<uint8_t>::FromExternal(kTestData));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_EQ(response.value()->actual_count, std::size(kTestData));
  }

  zx::eventpair event = GetEvent(client.value());
  ASSERT_OK(event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                           zx::time::infinite(), nullptr));
  {
    const fidl::WireResult result = client->Read(std::size(kTestData) + 10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
              std::string_view(reinterpret_cast<char*>(kTestData), std::size(kTestData)));
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                     zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);

  // Make sure we didn't see an INTERRUPT_EVENT.
  {
    auto result = control_client->ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, 0);
  }
}

TEST_F(PtyTestCase, ServerFillsClientFifo) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  zx::eventpair server_event = GetEvent(server);
  zx::eventpair client_event = GetEvent(client.value());

  uint8_t kTestString[] = "abcdefghijklmnopqrstuvwxyz";
  size_t total_written = 0;
  while (server_event.wait_one(
             static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable), zx::time{},
             nullptr) == ZX_OK) {
    const fidl::WireResult result = server->Write(
        fidl::VectorView<uint8_t>::FromExternal(kTestString, std::size(kTestString) - 1));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_GT(response.value()->actual_count, 0);
    total_written += response.value()->actual_count;
  }

  // Trying to write when full gets SHOULD_WAIT
  {
    const fidl::WireResult result = server->Write(
        fidl::VectorView<uint8_t>::FromExternal(kTestString, std::size(kTestString) - 1));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_error());
    ASSERT_STATUS(response.error_value(), ZX_ERR_SHOULD_WAIT);
  }

  // Client can read FIFO contents back out
  size_t total_read = 0;
  while (total_read < total_written) {
    ASSERT_OK(client_event.wait_one(
        static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable), zx::time{},
        nullptr));
    const fidl::WireResult result = client->Read(std::size(kTestString) - 1);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(data.count(), std::min(std::size(kTestString) - 1, total_written - total_read));
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
              std::string_view(reinterpret_cast<char*>(kTestString), data.count()));
    total_read += data.count();
  }

  ASSERT_STATUS(client_event.wait_one(
                    static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                    zx::time{}, nullptr),
                ZX_ERR_TIMED_OUT);
}

TEST_F(PtyTestCase, ClientFillsServerFifo) {
  Connection server{take_server()};
  zx::result client = OpenClient(server, 1);
  ASSERT_OK(client.status_value());

  zx::eventpair server_event = GetEvent(server);
  zx::eventpair client_event = GetEvent(client.value());

  uint8_t kTestString[] = "abcdefghijklmnopqrstuvwxyz";
  size_t total_written = 0;
  while (client_event.wait_one(
             static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable), zx::time{},
             nullptr) == ZX_OK) {
    const fidl::WireResult result = client->Write(
        fidl::VectorView<uint8_t>::FromExternal(kTestString, std::size(kTestString) - 1));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_GT(response.value()->actual_count, 0);
    total_written += response.value()->actual_count;
  }

  // Trying to write when full gets SHOULD_WAIT
  {
    const fidl::WireResult result = client->Write(
        fidl::VectorView<uint8_t>::FromExternal(kTestString, std::size(kTestString) - 1));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_error());
    ASSERT_STATUS(response.error_value(), ZX_ERR_SHOULD_WAIT);
  }

  // Server can read FIFO contents back out
  size_t total_read = 0;
  while (total_read < total_written) {
    ASSERT_OK(server_event.wait_one(
        static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable), zx::time{},
        nullptr));
    const fidl::WireResult result = server->Read(std::size(kTestString) - 1);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(data.count(), std::min(std::size(kTestString) - 1, total_written - total_read));
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.count()),
              std::string_view(reinterpret_cast<char*>(kTestString), data.count()));
    total_read += data.count();
  }

  ASSERT_STATUS(server_event.wait_one(
                    static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                    zx::time{}, nullptr),
                ZX_ERR_TIMED_OUT);
}

TEST_F(PtyTestCase, NonActiveClientsCantWrite) {
  Connection server{take_server()};
  zx::result control_client = OpenClient(server, 0);
  ASSERT_OK(control_client.status_value());
  zx::result other_client = OpenClient(server, 1);
  ASSERT_OK(other_client.status_value());

  // control_client is the current active

  zx::eventpair event = GetEvent(other_client.value());
  zx_signals_t observed = 0;
  ASSERT_STATUS(event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
  ASSERT_FALSE(observed & static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable));
  {
    uint8_t byte = 0;
    const fidl::WireResult result =
        other_client->Write(fidl::VectorView<uint8_t>::FromExternal(&byte, 1));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_error());
    ASSERT_STATUS(response.error_value(), ZX_ERR_SHOULD_WAIT);
  }
}

TEST_F(PtyTestCase, ClientsHaveIndependentFifos) {
  Connection server{take_server()};
  zx::result control_client = OpenClient(server, 0);
  ASSERT_OK(control_client.status_value());
  zx::result other_client = OpenClient(server, 1);
  ASSERT_OK(other_client.status_value());

  uint8_t kControlClientByte = 1;
  uint8_t kOtherClientByte = 2;

  // control_client is the current active, so it should go to its FIFO
  {
    const fidl::WireResult result =
        server->Write(fidl::VectorView<uint8_t>::FromExternal(&kControlClientByte, 1));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_EQ(response.value()->actual_count, 1);
  }

  // Switch active clients
  {
    auto result = control_client->MakeActive(1);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  // This should go to the other client's FIFO
  {
    const fidl::WireResult result =
        server->Write(fidl::VectorView<uint8_t>::FromExternal(&kOtherClientByte, 1));
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_EQ(response.value()->actual_count, 1);
  }

  auto check_client = [&](Connection& client, uint8_t expected_value) {
    zx::eventpair event = GetEvent(client);

    ASSERT_OK(
        event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                       zx::time{}, nullptr));

    const fidl::WireResult result = client->Read(10);
    ASSERT_OK(result.status());
    const fit::result response = result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    const fidl::VectorView data = response.value()->data;
    ASSERT_EQ(data.count(), 1);
    ASSERT_EQ(data.data()[0], expected_value);

    ASSERT_STATUS(
        event.wait_one(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable),
                       zx::time{}, nullptr),
        ZX_ERR_TIMED_OUT);
  };

  ASSERT_NO_FATAL_FAILURE(check_client(other_client.value(), kOtherClientByte));
  ASSERT_NO_FATAL_FAILURE(check_client(control_client.value(), kControlClientByte));
}

}  // namespace
