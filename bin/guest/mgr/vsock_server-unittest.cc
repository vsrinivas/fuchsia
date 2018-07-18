// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/vsock_server.h"

#include <unordered_set>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"
#include "garnet/bin/guest/mgr/remote_vsock_endpoint.h"
#include "lib/fxl/logging.h"
#include "lib/gtest/test_loop_fixture.h"

namespace guestmgr {
namespace {

class VsockServerTest : public ::gtest::TestLoopFixture {
 protected:
  VsockServer server;
};

struct TestConnection {
  zx::socket socket;
  zx_status_t status = ZX_ERR_BAD_STATE;

  fuchsia::guest::VsockConnector::ConnectCallback callback() {
    return [this](zx_status_t status, zx::handle handle) {
      this->status = status;
      this->socket = zx::socket(std::move(handle));
    };
  }
};

static void NoOpConnectCallback(zx_status_t status, zx::handle handle) {}

// A simple |fuchsia::guest::VsockAcceptor| that just retains a list of all
// connection requests.
class TestVsockAcceptor : public fuchsia::guest::VsockAcceptor {
 public:
  struct ConnectionRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    zx::handle handle;
    AcceptCallback callback;
  };

  ~TestVsockAcceptor() override = default;

  std::vector<ConnectionRequest> TakeRequests() {
    return std::move(connection_requests_);
  }

  fidl::InterfaceHandle<fuchsia::guest::VsockAcceptor> NewBinding() {
    return binding_.NewBinding();
  }

  // |fuchsia::guest::VsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              zx::handle handle, AcceptCallback callback) override {
    connection_requests_.emplace_back(ConnectionRequest{
        src_cid, src_port, port, std::move(handle), std::move(callback)});
  }

 private:
  std::vector<ConnectionRequest> connection_requests_;
  fidl::Binding<fuchsia::guest::VsockAcceptor> binding_{this};
};

class TestVsockEndpoint : public VsockEndpoint, public TestVsockAcceptor {
 public:
  TestVsockEndpoint(uint32_t cid) : VsockEndpoint(cid) {}

  // |fuchsia::guest::VsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              zx::handle handle, AcceptCallback callback) override {
    TestVsockAcceptor::Accept(src_cid, src_port, port, std::move(handle),
                              std::move(callback));
  }
};

TEST_F(VsockServerTest, RemoveEndpointOnDelete) {
  {
    RemoteVsockEndpoint endpoint(2);
    ASSERT_EQ(nullptr, server.FindEndpoint(2));
    ASSERT_EQ(ZX_OK, server.AddEndpoint(&endpoint));
    ASSERT_EQ(&endpoint, server.FindEndpoint(2));
  }

  // Delete |endpoint| and verify the server no longer resolves it.
  ASSERT_EQ(nullptr, server.FindEndpoint(2));
}

TEST_F(VsockServerTest, CreateEndpointDuplicateCid) {
  RemoteVsockEndpoint e1(2);
  RemoteVsockEndpoint e2(2);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&e1));
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, server.AddEndpoint(&e2));
}

// Test that endpoint with CID 2 connecting to endpoint with CID 3 gets routed
// through the VsockAcceptor for CID 3.
TEST_F(VsockServerTest, Connect) {
  RemoteVsockEndpoint cid2(2);
  RemoteVsockEndpoint cid3(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid2));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid3));

  // Setup acceptor to transfer to the caller.
  TestVsockAcceptor endpoint;
  cid3.SetVsockAcceptor(endpoint.NewBinding());
  RunLoopUntilIdle();

  // Request a connection on an arbitrary port.
  TestConnection connection;
  cid2.Connect(12345, 3, 1111, connection.callback());
  RunLoopUntilIdle();

  auto requests = endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(2u, request.src_cid);
  ASSERT_EQ(12345u, request.src_port);
  ASSERT_EQ(1111u, request.port);

  request.callback(ZX_OK);
  RunLoopUntilIdle();

  // Expect to have been transferred during the connect.
  ASSERT_EQ(ZX_OK, connection.status);
  ASSERT_TRUE(connection.socket.is_valid());
}

TEST_F(VsockServerTest, ConnectNoAcceptor) {
  VsockServer server;
  RemoteVsockEndpoint cid2(2);
  RemoteVsockEndpoint cid3(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid2));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid3));
  TestConnection connection;
  cid2.Connect(12345, 3, 1111, connection.callback());

  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  ASSERT_FALSE(connection.socket.is_valid());
}

TEST_F(VsockServerTest, ConnectInvalidCid) {
  RemoteVsockEndpoint endpoint(2);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&endpoint));

  TestConnection connection;
  endpoint.Connect(12345, 3, 1111, connection.callback());

  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  ASSERT_FALSE(connection.socket.is_valid());
}

TEST_F(VsockServerTest, HostConnect) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Verify the connection parameters as seen by |TestConnection| look good.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(request.src_cid, 2u);
  ASSERT_GE(request.src_port, kFirstEphemeralPort);
  ASSERT_EQ(request.port, 1111u);
}

TEST_F(VsockServerTest, HostConnectMultipleTimes) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Verify each connection has a distinct |src_port|.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(4u, requests.size());
  std::unordered_set<uint32_t> observed_ports;
  for (const auto& request : requests) {
    ASSERT_EQ(request.src_cid, 2u);
    ASSERT_GE(request.src_port, kFirstEphemeralPort);
    ASSERT_EQ(request.port, 1111u);
    ASSERT_EQ(observed_ports.find(request.src_port), observed_ports.end());
    observed_ports.insert(request.src_port);
  }
}

TEST_F(VsockServerTest, HostConnectFreeEphemeralPort) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Accept connection.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  auto request1 = std::move(requests[0]);
  ASSERT_EQ(request1.src_cid, 2u);
  ASSERT_GE(request1.src_port, kFirstEphemeralPort);
  ASSERT_EQ(request1.port, 1111u);
  request1.callback(ZX_OK);

  // Attempt another connection. Since request1 is still valid it should not
  // reuse the connection.
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  auto request2 = std::move(requests[0]);
  ASSERT_NE(request1.src_port, request2.src_port);
  ASSERT_GE(request2.src_port, kFirstEphemeralPort);

  // Close request1.
  request1.handle.reset();
  RunLoopUntilIdle();

  // Attempt a final connection. Expect |src_port| from the first request to
  // be recycled.
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  auto request3 = std::move(requests[0]);
  ASSERT_EQ(request1.src_port, request3.src_port);
}

TEST_F(VsockServerTest, HostListenOnConnectPort) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Verify connection request was delivered.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(request.src_cid, 2u);
  ASSERT_GE(request.src_port, kFirstEphemeralPort);
  ASSERT_EQ(request.port, 1111u);

  // We'll now try to listen on the port that is in use for the out-bound
  // connection to (3,1111). This should fail.
  TestVsockAcceptor acceptor;
  zx_status_t status = ZX_ERR_BAD_STATE;
  host_endpoint.Listen(request.src_port, acceptor.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, status);
}

TEST_F(VsockServerTest, HostListenTwice) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));
  zx_status_t status = ZX_ERR_BAD_STATE;

  // Listen 1 -- OK
  TestVsockAcceptor acceptor1;
  host_endpoint.Listen(22, acceptor1.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status);

  // Listen 2 -- Fail
  TestVsockAcceptor acceptor2;
  host_endpoint.Listen(22, acceptor2.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, status);
}

TEST_F(VsockServerTest, HostListenClose) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));
  zx_status_t status = ZX_ERR_BAD_STATE;

  // Setup listener on a port.
  TestVsockAcceptor acceptor;
  host_endpoint.Listen(22, acceptor.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status);

  // Verify listener is receiving connection requests.
  TestConnection connection;
  test_endpoint.Connect(12345, 2, 22, connection.callback());
  RunLoopUntilIdle();
  auto requests = acceptor.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(request.src_cid, 3u);
  ASSERT_GE(request.src_port, 12345u);
  ASSERT_EQ(request.port, 22u);

  // Now close the acceptor interface.
  acceptor.NewBinding();
  RunLoopUntilIdle();

  // Verify the endpoint responded to the channel close message by freeing up
  // the port.
  TestVsockAcceptor new_acceptor;
  status = ZX_ERR_BAD_STATE;
  host_endpoint.Listen(22, new_acceptor.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status);
}

}  // namespace
}  // namespace guestmgr
