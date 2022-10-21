// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"

namespace {

// Socket client to test the server
class TestClient {
 public:
  bool Connect(uint16_t port) {
    socket_.reset(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
    if (!socket_.is_valid()) {
      FX_LOGS(ERROR) << "Could not create socket.";
      return false;
    }
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port);
    if (connect(socket_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in6))) {
      FX_LOGS(ERROR) << "Could not connect to port - " << port;
      return false;
    }
    return true;
  }

  void Disconnect() { socket_.reset(); }

 private:
  fbl::unique_fd socket_;
};

constexpr uint16_t kServerPort = 15678;

}  // namespace

namespace zxdb {

class DebugAdapterServerTest : public TestWithLoop, public DebugAdapterServerObserver {
 public:
  DebugAdapterServerTest() : server_(&session_, kServerPort) { server_.AddObserver(this); }

  DebugAdapterServer& server() { return server_; }

  // DebugAdapterServerObserver methods. Quit loop to continue with the tests.
  void ClientConnected() override { debug::MessageLoop::Current()->QuitNow(); }
  void ClientDisconnected() override { debug::MessageLoop::Current()->QuitNow(); }

 private:
  Session session_;
  DebugAdapterServer server_;
};

TEST_F(DebugAdapterServerTest, InitTest) { server().Init(); }

TEST_F(DebugAdapterServerTest, ConnectionTest) {
  auto err = server().Init();
  ASSERT_FALSE(err.has_error());
  TestClient client;
  ASSERT_TRUE(client.Connect(kServerPort));
  // Loop is quit once the observer is notified of the connection
  debug::MessageLoop::Current()->Run();
  EXPECT_TRUE(server().IsConnected());
}

TEST_F(DebugAdapterServerTest, ConnectDisconnectTest) {
  auto err = server().Init();
  ASSERT_FALSE(err.has_error());
  TestClient client;
  ASSERT_TRUE(client.Connect(kServerPort));

  // Loop is quit once the observer is notified of the connection
  debug::MessageLoop::Current()->Run();
  ASSERT_TRUE(server().IsConnected());
  client.Disconnect();

  // Loop is quit once the observer is notified of the disconnection
  debug::MessageLoop::Current()->Run();
  EXPECT_FALSE(server().IsConnected());
}

}  // namespace zxdb
