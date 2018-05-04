// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/vsock_server.h"

#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"

namespace guestmgr {
namespace {

class VsockServerTest : public testing::Test {
 protected:
  VsockServer server;
  async::Loop loop{&kAsyncLoopConfigMakeDefault};
};

// Helper for providing an |InterfaceHandle| bound to a std::function that will
// be invoked on each connection request.
//
// Use |loop.RunUntilIdle| in the test fixture to synchronously process any
// pending FIDL messages on the underlying channel.
class TestSocketAcceptor : public guest::SocketAcceptor {
 public:
  using Delegate =
      std::function<void(uint32_t, uint32_t, uint32_t, AcceptCallback)>;
  TestSocketAcceptor(Delegate d) : delegate_(std::move(d)) {}
  ~TestSocketAcceptor() override = default;

  // |guest::SocketAcceptor|
  void Accept(uint32_t src_cid,
              uint32_t src_port,
              uint32_t port,
              AcceptCallback callback) override {
    delegate_(src_cid, src_port, port, std::move(callback));
  };

  fidl::InterfaceHandle<guest::SocketAcceptor> NewBinding() {
    return binding_.NewBinding();
  }

 private:
  Delegate delegate_;
  fidl::Binding<guest::SocketAcceptor> binding_{this};
};

TEST_F(VsockServerTest, RemoveEndpointOnDelete) {
  {
    std::unique_ptr<VsockEndpoint> endpoint;
    ASSERT_EQ(nullptr, server.FindEndpoint(2));
    ASSERT_EQ(ZX_OK, server.CreateEndpoint(2, &endpoint));
    ASSERT_EQ(endpoint.get(), server.FindEndpoint(2));
  }

  // Delete |endpoint| and verify the server no longer resolves it.
  ASSERT_EQ(nullptr, server.FindEndpoint(2));
}

TEST_F(VsockServerTest, CreateEndpointDuplicateCid) {
  std::unique_ptr<VsockEndpoint> e1;
  std::unique_ptr<VsockEndpoint> e2;
  ASSERT_EQ(ZX_OK, server.CreateEndpoint(2, &e1));
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, server.CreateEndpoint(2, &e2));
}

// Test that endpoint with CID 2 connecting to endpoint with CID 3 gets routed
// through the SocketAcceptor for CID 3.
TEST_F(VsockServerTest, Connect) {
  std::unique_ptr<VsockEndpoint> cid2;
  std::unique_ptr<VsockEndpoint> cid3;
  ASSERT_EQ(ZX_OK, server.CreateEndpoint(2, &cid2));
  ASSERT_EQ(ZX_OK, server.CreateEndpoint(3, &cid3));

  // Setup acceptor to transfer |h2| to the caller.
  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));
  TestSocketAcceptor endpoint(
      [&](uint32_t src_cid, uint32_t src_port, uint32_t port,
          guest::SocketAcceptor::AcceptCallback callback) {
        ASSERT_EQ(2u, src_cid);
        ASSERT_EQ(12345u, src_port);
        ASSERT_EQ(1111u, port);
        ASSERT_TRUE(h2.is_valid());
        callback(ZX_OK, std::move(h2));
      });
  cid3->SetSocketAcceptor(endpoint.NewBinding());
  loop.RunUntilIdle();

  // Request a connection on an arbitrary port.
  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::socket socket;
  cid2->Connect(12345, 3, 1111, [&](zx_status_t _status, zx::socket _socket) {
    status = _status;
    socket = std::move(_socket);
  });
  loop.RunUntilIdle();

  // Expect |h2| to have been transferred during the connect.
  ASSERT_EQ(ZX_OK, status);
  ASSERT_FALSE(h2.is_valid());
  ASSERT_TRUE(socket.is_valid());
}

TEST_F(VsockServerTest, ConnectNoAcceptor) {
  VsockServer server;
  std::unique_ptr<VsockEndpoint> cid2;
  std::unique_ptr<VsockEndpoint> cid3;
  ASSERT_EQ(ZX_OK, server.CreateEndpoint(2, &cid2));
  ASSERT_EQ(ZX_OK, server.CreateEndpoint(3, &cid3));
  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::socket socket;
  cid2->Connect(12345, 3, 1111, [&](zx_status_t _status, zx::socket _socket) {
    status = _status;
    socket = std::move(_socket);
  });

  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, status);
  ASSERT_FALSE(socket.is_valid());
}

TEST_F(VsockServerTest, ConnectInvalidCid) {
  std::unique_ptr<VsockEndpoint> endpoint;
  ASSERT_EQ(ZX_OK, server.CreateEndpoint(2, &endpoint));

  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::socket socket;
  endpoint->Connect(12345, 3, 1111,
                    [&](zx_status_t _status, zx::socket _socket) {
                      status = _status;
                      socket = std::move(_socket);
                    });
  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, status);
  ASSERT_FALSE(socket.is_valid());
}

}  // namespace
}  // namespace guestmgr
