// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <fs/managed_vfs.h>
#include <fs/vfs_types.h>
#include <zxtest/zxtest.h>

#include "pty-server-vnode.h"
#include "pty-server.h"

namespace {

using Connection = ::llcpp::fuchsia::hardware::pty::Device::SyncClient;

class PtyTestCase : public zxtest::Test {
 public:
  PtyTestCase()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        vfs_(loop_.dispatcher()),
        server_(zx::channel()) {}

  void SetUp() override {
    ASSERT_OK(loop_.StartThread("pty-test"));
    ASSERT_NO_FATAL_FAILURES(CreateNewServer(&server_));
  }
  void TearDown() override {
    sync_completion_t completion;
    vfs_.Shutdown([&completion](zx_status_t) { sync_completion_signal(&completion); });
    ASSERT_OK(sync_completion_wait_deadline(&completion, zx::time::infinite().get()));
  }

 protected:
  zx_status_t OpenClient(Connection* conn, uint32_t id, Connection* client) {
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }
    auto result = conn->OpenClient(id, std::move(remote));
    if (result.status() != ZX_OK) {
      return ZX_ERR_BAD_STATE;
    }
    if (result->s != ZX_OK) {
      return result->s;
    }
    *client = Connection(std::move(local));
    return ZX_OK;
  }

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  fs::Vfs* vfs() { return &vfs_; }
  Connection take_server() { return std::move(server_); }

 private:
  void CreateNewServer(Connection* conn) {
    fbl::RefPtr<PtyServer> server;
    ASSERT_OK(PtyServer::Create(&server, &vfs_));
    auto vnode = fbl::MakeRefCounted<PtyServerVnode>(std::move(server));

    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ASSERT_OK(
        vfs()->Serve(std::move(vnode), std::move(remote), fs::VnodeConnectionOptions::ReadWrite()));
    *conn = Connection(std::move(local));
  }

  async::Loop loop_;
  fs::ManagedVfs vfs_;
  Connection server_;
};

zx::eventpair GetEvent(Connection* conn) {
  auto result = conn->Describe();
  if (result.status() != ZX_OK) {
    return {};
  }
  zx::eventpair event = std::move(result->info.mutable_tty().event);
  return event;
}

void WriteCtrlC(Connection* conn) {
  uint8_t data[] = {0x03};
  auto result = conn->Write(fidl::VectorView<uint8_t>{data, fbl::count_of(data)});
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);
  ASSERT_EQ(result->actual, fbl::count_of(data));
}

// Make sure the server connections describe appropriately
TEST_F(PtyTestCase, ServerDescribe) {
  Connection server{take_server()};
  auto result = server.Describe();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->info.is_tty());
  ASSERT_TRUE(result->info.tty().event.is_valid());
}  // namespace

TEST_F(PtyTestCase, ServerSetWindowSize) {
  Connection server{take_server()};
  auto result = server.SetWindowSize({.width = 80, .height = 24});
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
}  // namespace

TEST_F(PtyTestCase, ServerClrSetFeature) {
  Connection server{take_server()};
  auto result = server.ClrSetFeature(0, 0);
  ASSERT_OK(result.status());
  // ClrSetFeature is only meaningful on clients
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PtyTestCase, ServerGetWindowSize) {
  Connection server{take_server()};
  auto result = server.GetWindowSize();
  ASSERT_OK(result.status());
  // Our original implementation didn't support this, so preserve that behavior.
  // It's not clear why, though.  If this is causing problems, we should
  // probably just implement it.
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PtyTestCase, ServerMakeActive) {
  Connection server{take_server()};
  auto result = server.MakeActive(0);
  ASSERT_OK(result.status());
  // MakeActive is only meaningful on clients
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PtyTestCase, ServerReadEvents) {
  Connection server{take_server()};
  auto result = server.ReadEvents();
  ASSERT_OK(result.status());
  // ReadEvents is only meaningful on clients
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
}

// Basic test of opening a client
TEST_F(PtyTestCase, ServerBasicOpenClient) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  // Make sure our client connection is valid after this
  ASSERT_STATUS(client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
                ZX_ERR_TIMED_OUT);
}

// Try opening two clients with the same id
TEST_F(PtyTestCase, ServerOpenClientTwice) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));
  Connection client2{zx::channel{}};
  ASSERT_STATUS(OpenClient(&server, 0, &client2), ZX_ERR_INVALID_ARGS);

  // Our original client connection should still be good.
  ASSERT_STATUS(client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
                ZX_ERR_TIMED_OUT);
}

// Try opening two clients with different ids
TEST_F(PtyTestCase, ServerOpenClientTwoDifferent) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));
  Connection client2{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client2));

  // Both connections should be good
  ASSERT_STATUS(client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
                ZX_ERR_TIMED_OUT);
  ASSERT_STATUS(client2.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time{}, nullptr),
                ZX_ERR_TIMED_OUT);
}

// Verify a server with no clients behaves as expected
TEST_F(PtyTestCase, ServerWithNoClientsInitialConditions) {
  Connection server{take_server()};
  zx::eventpair event = GetEvent(&server);

  auto check_state = [&]() {
    zx_signals_t observed = 0;
    ASSERT_STATUS(event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
    // Precisely this set of signals should be asserted
    ASSERT_EQ(observed, ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE |
                            ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP);

    // Attempts to read should get 0 bytes and ZX_OK
    {
      auto result = server.Read(10);
      ASSERT_OK(result.status());
      ASSERT_OK(result->s);
      ASSERT_EQ(result->data.count(), 0);
    }

    // Attempts to write should fail with ZX_ERR_PEER_CLOSED
    {
      uint8_t data[16] = {};
      auto result = server.Write(fidl::VectorView<uint8_t>{data, fbl::count_of(data)});
      ASSERT_OK(result.status());
      ASSERT_STATUS(result->s, ZX_ERR_PEER_CLOSED);
    }
  };

  ASSERT_NO_FATAL_FAILURES(check_state());

  // Create a client and close it, then make sure we're back in the initial
  // state
  {
    Connection client{zx::channel{}};
    ASSERT_OK(OpenClient(&server, 1, &client));
  }
  // Wait for the server to signal that it got the client disconnect
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP, zx::time::infinite(),
                           nullptr));

  ASSERT_NO_FATAL_FAILURES(check_state());
}

// Verify a server with a client has the right state
TEST_F(PtyTestCase, ServerWithClientInitialConditions) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  zx::eventpair server_event = GetEvent(&server);
  zx::eventpair client_event = GetEvent(&client);

  zx_signals_t observed = 0;
  ASSERT_STATUS(server_event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
  ASSERT_EQ(observed, ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE);

  observed = 0;
  ASSERT_STATUS(client_event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
  ASSERT_EQ(observed, ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE);

  // Attempts to read on either side should get SHOULD_WAIT
  {
    auto result = server.Read(10);
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->s, ZX_ERR_SHOULD_WAIT);
  }
  {
    auto result = client.Read(10);
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->s, ZX_ERR_SHOULD_WAIT);
  }

  // Client should be in cooked mode
  {
    auto result = client.ClrSetFeature(0, 0);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->features, 0);
  }
}

// Verify a read from a server for 0 bytes doesn't return ZX_ERR_SHOULD_WAIT
TEST_F(PtyTestCase, ServerEmpty0ByteRead) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  auto result = server.Read(0);
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);
  ASSERT_EQ(result->data.count(), 0);
}

// Verify a write by the server for 0 bytes when the receiving client is full doesn't return
// ZX_ERR_SHOULD_WAIT
TEST_F(PtyTestCase, ClientFull0ByteServerWrite) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  // Fill up FIFO
  while (true) {
    uint8_t buf[256] = {};
    auto result = server.Write({buf, fbl::count_of(buf)});
    ASSERT_OK(result.status());
    if (result->s == ZX_ERR_SHOULD_WAIT) {
      break;
    }
    ASSERT_OK(result->s);
    ASSERT_GT(result->actual, 0);
  }

  auto result = server.Write({});
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);
  ASSERT_EQ(result->actual, 0);
}

// Verify a write by a client for 0 bytes when the client isn't active returns
// ZX_ERR_SHOULD_WAIT
TEST_F(PtyTestCase, ClientInactive0ByteClientWrite) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));
  Connection inactive_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &inactive_client));

  auto result = inactive_client.Write({});
  ASSERT_OK(result.status());
  ASSERT_STATUS(result->s, ZX_ERR_SHOULD_WAIT);
}

// Make sure the client connections describe appropriately
TEST_F(PtyTestCase, ClientDescribe) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  auto result = client.Describe();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->info.is_tty());
  ASSERT_TRUE(result->info.tty().event.is_valid());
}

TEST_F(PtyTestCase, ClientWindowSize) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  {
    auto result = server.SetWindowSize({.width = 80, .height = 24});
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
  {
    auto result = client.GetWindowSize();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->size.width, 80);
    ASSERT_EQ(result->size.height, 24);
  }
  {
    auto result = client.SetWindowSize({.width = 5, .height = 32});
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
  {
    auto result = client.GetWindowSize();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->size.width, 5);
    ASSERT_EQ(result->size.height, 32);
  }
}

TEST_F(PtyTestCase, ClientClrSetFeature) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  auto result = client.ClrSetFeature(0, 0);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->features, 0);

  // Make sure we can set bits
  result = client.ClrSetFeature(0, ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->features, ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);

  // If we don't change any bits, we should see the new settings
  result = client.ClrSetFeature(0, 0);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->features, ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);

  // Make sure we can clear bits
  result = client.ClrSetFeature(::llcpp::fuchsia::hardware::pty::FEATURE_RAW, 0);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->features, 0);
}

TEST_F(PtyTestCase, ClientClrSetFeatureInvalidBit) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  auto result = client.ClrSetFeature(0, 0x2);
  ASSERT_OK(result.status());
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result->features, 0);

  result = client.ClrSetFeature(0x2, 0);
  ASSERT_OK(result.status());
  ASSERT_STATUS(result->status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result->features, 0);
}

TEST_F(PtyTestCase, ClientGetWindowSizeServerNeverSet) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  auto result = client.GetWindowSize();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->size.width, 0);
  ASSERT_EQ(result->size.height, 0);
}

// Each client should have its own feature flags
TEST_F(PtyTestCase, ClientIndependentFeatureFlags) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));
  Connection client2{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client2));

  auto result = client.ClrSetFeature(0, ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->features, ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);

  // Client 2 shouldn't see the changes
  result = client2.ClrSetFeature(0, 0);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->features, 0);
}

TEST_F(PtyTestCase, ClientMakeActive) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));
  Connection client2{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client2));

  auto result = client.MakeActive(0);
  ASSERT_OK(result.status());
  // This client is not the controlling client (id=0), so it cannot change the
  // active client
  ASSERT_STATUS(result->status, ZX_ERR_ACCESS_DENIED);

  result = client2.MakeActive(1);
  ASSERT_OK(result.status());
  // This client is the controlling client (id=0), so it can.
  ASSERT_OK(result->status);

  // Changing the active client to the existing active client should be fine
  result = client2.MakeActive(1);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);

  // Changing the active client to the control client should be fine
  result = client2.MakeActive(0);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);

  // Changing the active client to a non-existent client should fail
  result = client2.MakeActive(2);
  ASSERT_OK(result.status());
  ASSERT_STATUS(result->status, ZX_ERR_NOT_FOUND);
}

TEST_F(PtyTestCase, ClientReadEvents) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));
  Connection client2{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client2));

  auto result = client.ReadEvents();
  ASSERT_OK(result.status());
  // This client is not the controlling client (id=0), so it cannot read events
  ASSERT_STATUS(result->status, ZX_ERR_ACCESS_DENIED);

  result = client2.ReadEvents();
  ASSERT_OK(result.status());
  // This client is the controlling client (id=0), so it can read events
  ASSERT_OK(result->status);
  ASSERT_EQ(result->events, 0);
}

// Reading events should clear the event condition
TEST_F(PtyTestCase, ClientReadEventsClears) {
  Connection server{take_server()};
  Connection active_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &active_client));
  Connection control_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &control_client));

  zx::eventpair control_event = GetEvent(&control_client);

  // No events yet
  ASSERT_STATUS(
      control_event.wait_one(::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);

  // Write a ^C byte from the server to trigger a cooked-mode event
  ASSERT_NO_FATAL_FAILURES(WriteCtrlC(&server));

  ASSERT_OK(control_event.wait_one(::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT,
                                   zx::time::infinite(), nullptr));

  {
    auto result = control_client.ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, ::llcpp::fuchsia::hardware::pty::EVENT_INTERRUPT);
  }

  // Signal should have cleared
  ASSERT_STATUS(
      control_event.wait_one(::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);

  // Event should have cleared
  {
    auto result = control_client.ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, 0);
  }
}

// Events arrive even without a controlling client connected
TEST_F(PtyTestCase, EventsSentWithNoControllingClient) {
  Connection server{take_server()};
  Connection active_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &active_client));

  // Write a ^C byte from the server to trigger a cooked-mode event
  ASSERT_NO_FATAL_FAILURES(WriteCtrlC(&server));

  // Connect a control client to inspect the event
  Connection control_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &control_client));

  zx::eventpair control_event = GetEvent(&control_client);
  ASSERT_OK(
      control_event.wait_one(::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT, zx::time{}, nullptr));

  {
    auto result = control_client.ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, ::llcpp::fuchsia::hardware::pty::EVENT_INTERRUPT);
  }
}

TEST_F(PtyTestCase, NonControllingClientOpenClient) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  Connection client2{zx::channel{}};
  // This client is not the controlling client (id=0), so it cannot create new
  // clients
  ASSERT_STATUS(OpenClient(&client, 2, &client2), ZX_ERR_ACCESS_DENIED);
}

TEST_F(PtyTestCase, ControllingClientOpenClient) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  Connection client2{zx::channel{}};
  ASSERT_OK(OpenClient(&client, 1, &client2));
}

TEST_F(PtyTestCase, ActiveClientCloses) {
  Connection server{take_server()};
  Connection control_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &control_client));
  {
    Connection active_client{zx::channel{}};
    ASSERT_OK(OpenClient(&server, 1, &active_client));
    auto result = control_client.MakeActive(1);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  zx::eventpair control_event = GetEvent(&control_client);
  zx_signals_t observed = 0;
  ASSERT_OK(control_event.wait_one(::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT,
                                   zx::time::infinite(), &observed));
  // Wait again with no timeout, so that observed doesn't have any transient
  // signals in it.
  ASSERT_OK(control_event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP, zx::time{},
                                   &observed));
  ASSERT_EQ(observed, ::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT |
                          ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP);

  auto result = control_client.ReadEvents();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_EQ(result->events, ::llcpp::fuchsia::hardware::pty::EVENT_HANGUP);
}

// Makes sure nothing goes wrong when the active client is the controling
// client and it closes.
TEST_F(PtyTestCase, ActiveClientClosesWhenControl) {
  Connection server{take_server()};
  {
    Connection control_client{zx::channel{}};
    ASSERT_OK(OpenClient(&server, 0, &control_client));
  }
  zx::eventpair event = GetEvent(&server);
  zx_signals_t observed = 0;
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP, zx::time::infinite(),
                           &observed));
}

TEST_F(PtyTestCase, ServerClosesWhenClientPresent) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &client));

  // Write some data to the client, so we can verify the client can drain the
  // buffer still.

  uint8_t kTestData[] = u8"hello world";
  {
    auto result = server.Write(fidl::VectorView<uint8_t>{kTestData, fbl::count_of(kTestData)});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual, fbl::count_of(kTestData));
  }

  server.mutable_channel()->reset();

  zx::eventpair event = GetEvent(&client);
  zx_signals_t observed = 0;
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP, zx::time::infinite(),
                           &observed));
  // Wait again with no timeout, so that observed doesn't have any transient
  // signals in it.
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP, zx::time{}, &observed));
  ASSERT_EQ(observed, ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP |
                          ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE);

  {
    auto result = client.ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, ::llcpp::fuchsia::hardware::pty::EVENT_HANGUP);
  }

  // Attempts to drain the buffer should succeed
  {
    // Request more bytes than are present
    auto result = client.Read(fbl::count_of(kTestData) + 10);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(), fbl::count_of(kTestData));
    ASSERT_BYTES_EQ(result->data.data(), kTestData, fbl::count_of(kTestData));
  }

  // Attempts to read the empty buffer should fail with ZX_ERR_PEER_CLOSED
  {
    auto result = client.Read(10);
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->s, ZX_ERR_PEER_CLOSED);
  }

  // Attempts to write should fail with ZX_ERR_PEER_CLOSED
  {
    uint8_t data[16] = {};
    auto result = client.Write(fidl::VectorView<uint8_t>{data, fbl::count_of(data)});
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->s, ZX_ERR_PEER_CLOSED);
  }
}

// Test writes from the client to the server when the client is cooked
TEST_F(PtyTestCase, ServerReadClientCooked) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  // In cooked mode, client writes should have \n transformed to \r\n, and
  // control chars untouched.
  uint8_t kTestData[] = u8"hello\x03 world\ntest message\n";
  const uint8_t kExpectedReadback[] = u8"hello\x03 world\r\ntest message\r\n";
  {
    auto result = client.Write(fidl::VectorView<uint8_t>{kTestData, fbl::count_of(kTestData)});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual, fbl::count_of(kTestData));
  }

  zx::eventpair event = GetEvent(&server);
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time::infinite(),
                           nullptr));
  {
    auto result = server.Read(fbl::count_of(kExpectedReadback) + 10);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(), fbl::count_of(kExpectedReadback));
    ASSERT_BYTES_EQ(result->data.data(), kExpectedReadback, fbl::count_of(kExpectedReadback));
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Test writes from the server to the client when the client is cooked
TEST_F(PtyTestCase, ServerWriteClientCooked) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  // In cooked mode, server writes should have newlines untouched and control
  // chars should cause a short write
  uint8_t kTestData[] = u8"hello world\ntest\x03 message\n";
  // We expect to read this back, but without the trailing nul
  const uint8_t kExpectedReadbackWithNul[] = u8"hello world\ntest";
  {
    auto result = server.Write(fidl::VectorView<uint8_t>{kTestData, fbl::count_of(kTestData)});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    // We expect to see the written count to include the ^C
    ASSERT_EQ(result->actual, fbl::count_of(kExpectedReadbackWithNul) - 1 + 1);
  }

  zx::eventpair event = GetEvent(&client);
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time::infinite(),
                           nullptr));
  {
    auto result = client.Read(fbl::count_of(kExpectedReadbackWithNul) + 10);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(), fbl::count_of(kExpectedReadbackWithNul) - 1);
    ASSERT_BYTES_EQ(result->data.data(), kExpectedReadbackWithNul,
                    fbl::count_of(kExpectedReadbackWithNul) - 1);
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Test writes from the client to the server when the client is raw
TEST_F(PtyTestCase, ServerReadClientRaw) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  {
    auto result = client.ClrSetFeature(0, ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  // In raw mode, client writes should be untouched.
  uint8_t kTestData[] = u8"hello\x03 world\ntest message\n";
  {
    auto result = client.Write(fidl::VectorView<uint8_t>{kTestData, fbl::count_of(kTestData)});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual, fbl::count_of(kTestData));
  }

  zx::eventpair event = GetEvent(&server);
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time::infinite(),
                           nullptr));
  {
    auto result = server.Read(fbl::count_of(kTestData) + 10);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(), fbl::count_of(kTestData));
    ASSERT_BYTES_EQ(result->data.data(), kTestData, fbl::count_of(kTestData));
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

// Test writes from the server to the client when the client is raw
TEST_F(PtyTestCase, ServerWriteClientRaw) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));
  Connection control_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &control_client));

  {
    auto result = client.ClrSetFeature(0, ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  // In raw mode, server writes should be untouched.
  uint8_t kTestData[] = u8"hello world\ntest\x03 message\n";
  {
    auto result = server.Write(fidl::VectorView<uint8_t>{kTestData, fbl::count_of(kTestData)});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual, fbl::count_of(kTestData));
  }

  zx::eventpair event = GetEvent(&client);
  ASSERT_OK(event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time::infinite(),
                           nullptr));
  {
    auto result = client.Read(fbl::count_of(kTestData) + 10);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(), fbl::count_of(kTestData));
    ASSERT_BYTES_EQ(result->data.data(), kTestData, fbl::count_of(kTestData));
  }
  // Nothing left to read
  ASSERT_STATUS(
      event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);

  // Make sure we didn't see an INTERRUPT_EVENT.
  {
    auto result = control_client.ReadEvents();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
    ASSERT_EQ(result->events, 0);
  }
}

TEST_F(PtyTestCase, ServerFillsClientFifo) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  zx::eventpair server_event = GetEvent(&server);
  zx::eventpair client_event = GetEvent(&client);

  uint8_t kTestString[] = "abcdefghijklmnopqrstuvwxyz";
  size_t total_written = 0;
  while (server_event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE, zx::time{},
                               nullptr) == ZX_OK) {
    auto result =
        server.Write(fidl::VectorView<uint8_t>{kTestString, fbl::count_of(kTestString) - 1});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_GT(result->actual, 0);
    total_written += result->actual;
  }

  // Trying to write when full gets SHOULD_WAIT
  {
    auto result =
        server.Write(fidl::VectorView<uint8_t>{kTestString, fbl::count_of(kTestString) - 1});
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->s, ZX_ERR_SHOULD_WAIT);
  }

  // Client can read FIFO contents back out
  size_t total_read = 0;
  while (total_read < total_written) {
    ASSERT_OK(client_event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{},
                                    nullptr));
    auto result = client.Read(fbl::count_of(kTestString) - 1);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(),
              fbl::min(fbl::count_of(kTestString) - 1, total_written - total_read));
    ASSERT_BYTES_EQ(result->data.data(), kTestString, result->data.count());
    total_read += result->data.count();
  }

  ASSERT_STATUS(
      client_event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

TEST_F(PtyTestCase, ClientFillsServerFifo) {
  Connection server{take_server()};
  Connection client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &client));

  zx::eventpair server_event = GetEvent(&server);
  zx::eventpair client_event = GetEvent(&client);

  uint8_t kTestString[] = "abcdefghijklmnopqrstuvwxyz";
  size_t total_written = 0;
  while (client_event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE, zx::time{},
                               nullptr) == ZX_OK) {
    auto result =
        client.Write(fidl::VectorView<uint8_t>{kTestString, fbl::count_of(kTestString) - 1});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_GT(result->actual, 0);
    total_written += result->actual;
  }

  // Trying to write when full gets SHOULD_WAIT
  {
    auto result =
        client.Write(fidl::VectorView<uint8_t>{kTestString, fbl::count_of(kTestString) - 1});
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->s, ZX_ERR_SHOULD_WAIT);
  }

  // Server can read FIFO contents back out
  size_t total_read = 0;
  while (total_read < total_written) {
    ASSERT_OK(server_event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{},
                                    nullptr));
    auto result = server.Read(fbl::count_of(kTestString) - 1);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(),
              fbl::min(fbl::count_of(kTestString) - 1, total_written - total_read));
    ASSERT_BYTES_EQ(result->data.data(), kTestString, result->data.count());
    total_read += result->data.count();
  }

  ASSERT_STATUS(
      server_event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr),
      ZX_ERR_TIMED_OUT);
}

TEST_F(PtyTestCase, NonActiveClientsCantWrite) {
  Connection server{take_server()};
  Connection control_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &control_client));
  Connection other_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &other_client));

  // control_client is the current active

  zx::eventpair event = GetEvent(&other_client);
  zx_signals_t observed = 0;
  ASSERT_STATUS(event.wait_one(0, zx::time{}, &observed), ZX_ERR_TIMED_OUT);
  ASSERT_FALSE(observed & ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE);
  {
    uint8_t byte = 0;
    auto result = other_client.Write(fidl::VectorView<uint8_t>{&byte, 1});
    ASSERT_OK(result.status());
    ASSERT_STATUS(result->s, ZX_ERR_SHOULD_WAIT);
  }
}

TEST_F(PtyTestCase, ClientsHaveIndependentFifos) {
  Connection server{take_server()};
  Connection control_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 0, &control_client));
  Connection other_client{zx::channel{}};
  ASSERT_OK(OpenClient(&server, 1, &other_client));

  uint8_t kControlClientByte = 1;
  uint8_t kOtherClientByte = 2;

  // control_client is the current active, so it should go to its FIFO
  {
    auto result = server.Write(fidl::VectorView<uint8_t>{&kControlClientByte, 1});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual, 1);
  }

  // Switch active clients
  {
    auto result = control_client.MakeActive(1);
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  // This should go to the other client's FIFO
  {
    auto result = server.Write(fidl::VectorView<uint8_t>{&kOtherClientByte, 1});
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual, 1);
  }

  auto check_client = [&](Connection* client, uint8_t expected_value) {
    zx::eventpair event = GetEvent(client);

    ASSERT_OK(
        event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr));

    auto result = client->Read(10);
    ASSERT_OK(result.status());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->data.count(), 1);
    ASSERT_EQ(result->data.data()[0], expected_value);

    ASSERT_STATUS(
        event.wait_one(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, zx::time{}, nullptr),
        ZX_ERR_TIMED_OUT);
  };

  ASSERT_NO_FATAL_FAILURES(check_client(&other_client, kOtherClientByte));
  ASSERT_NO_FATAL_FAILURES(check_client(&control_client, kControlClientByte));
}

}  // namespace
