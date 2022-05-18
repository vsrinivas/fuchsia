// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/host_vsock/host_vsock_endpoint.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/test_loop_fixture.h>

#include <unordered_map>

#include "src/lib/fsl/handles/object_info.h"
#include "src/virtualization/bin/host_vsock/guest_vsock_endpoint.h"

namespace {

using ::fuchsia::virtualization::HostVsockConnector_Connect_Result;
using ::fuchsia::virtualization::HostVsockEndpoint_Connect2_Result;
using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;

constexpr uint32_t kGuestCid = 3;
constexpr uint32_t kOtherGuestCid = 4;

template <typename T>
class TestVsockAcceptorBase : public T {
 public:
  struct ConnectionRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    zx::socket socket;
    typename T::AcceptCallback callback;
  };

  fidl::InterfaceHandle<T> NewBinding() { return binding_.NewBinding(); }

  std::vector<ConnectionRequest> TakeRequests() { return std::move(connection_requests_); }

 protected:
  fidl::Binding<T> binding_{this};
  std::vector<ConnectionRequest> connection_requests_;
};

class TestVsockAcceptor
    : public TestVsockAcceptorBase<fuchsia::virtualization::GuestVsockAcceptor> {
 private:
  // |fuchsia::virtualization::GuestVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port, zx::socket socket,
              AcceptCallback callback) override {
    connection_requests_.emplace_back(
        ConnectionRequest{src_cid, src_port, port, std::move(socket), std::move(callback)});
  }
};

class TestHostVsockAcceptor
    : public TestVsockAcceptorBase<fuchsia::virtualization::HostVsockAcceptor> {
 private:
  // |fuchsia::virtualization::HostVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override {
    connection_requests_.emplace_back(
        ConnectionRequest{src_cid, src_port, port, zx::socket(), std::move(callback)});
  }
};

struct TestConnectorConnection {
  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::socket socket;

  fuchsia::virtualization::HostVsockConnector::ConnectCallback callback() {
    return [this](HostVsockConnector_Connect_Result result) {
      if (result.is_response()) {
        this->status = ZX_OK;
        this->socket = std::move(result.response().socket);
      } else {
        this->status = result.err();
        this->socket = zx::socket();
      }
    };
  }
};

struct TestEndpointConnection {
  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::socket socket;

  fuchsia::virtualization::HostVsockEndpoint::Connect2Callback connect2_callback() {
    return [this](HostVsockEndpoint_Connect2_Result result) {
      if (result.is_response()) {
        this->status = ZX_OK;
        this->socket = std::move(result.response().socket);
      } else {
        this->status = result.err();
      }
    };
  }

  fuchsia::virtualization::HostVsockEndpoint::ListenCallback listen_callback() {
    return [this](HostVsockEndpoint_Listen_Result result) {
      this->status = result.is_response() ? ZX_OK : result.err();
    };
  }
};

void NoOpCallback(HostVsockEndpoint_Listen_Result result) {}

class HostVsockEndpointTest : public ::gtest::TestLoopFixture {
 protected:
  HostVsockEndpoint host_endpoint_{dispatcher(),
                                   fit::bind_member(this, &HostVsockEndpointTest::GetAcceptor)};
  TestVsockAcceptor guest_acceptor_;

 private:
  fuchsia::virtualization::GuestVsockAcceptor* GetAcceptor(uint32_t cid) {
    return cid == kGuestCid ? &guest_acceptor_ : nullptr;
  }
};

TEST_F(HostVsockEndpointTest, ConnectGuestToGuest) {
  TestConnectorConnection connection;
  host_endpoint_.Connect(kOtherGuestCid, 1022, kGuestCid, 22, connection.callback());

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(kOtherGuestCid, requests[0].src_cid);
  EXPECT_EQ(1022u, requests[0].src_port);
  EXPECT_EQ(22u, requests[0].port);
  EXPECT_TRUE(requests[0].socket.is_valid());

  requests[0].callback(fuchsia::virtualization::GuestVsockAcceptor_Accept_Result::WithResponse({}));

  EXPECT_EQ(ZX_OK, connection.status);
  EXPECT_TRUE(connection.socket.is_valid());
}

TEST_F(HostVsockEndpointTest, ConnectGuestToHost) {
  TestHostVsockAcceptor host_acceptor;
  host_endpoint_.Listen(22, host_acceptor.NewBinding(), NoOpCallback);

  TestConnectorConnection connection;
  host_endpoint_.Connect(kGuestCid, 1022, fuchsia::virtualization::HOST_CID, 22,
                         connection.callback());

  RunLoopUntilIdle();

  auto requests = host_acceptor.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(kGuestCid, requests[0].src_cid);
  EXPECT_EQ(1022u, requests[0].src_port);
  EXPECT_EQ(22u, requests[0].port);

  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));

  requests[0].callback(fpromise::ok((std::move(h1))));

  RunLoopUntilIdle();

  EXPECT_EQ(ZX_OK, connection.status);
  EXPECT_TRUE(connection.socket.is_valid());
}

TEST_F(HostVsockEndpointTest, Connect2HostToGuest) {
  TestEndpointConnection connection;
  host_endpoint_.Connect2(22, connection.connect2_callback());

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(fuchsia::virtualization::HOST_CID, requests[0].src_cid);
  EXPECT_EQ(kFirstEphemeralPort, requests[0].src_port);
  EXPECT_EQ(22u, requests[0].port);
  EXPECT_TRUE(requests[0].socket.is_valid());

  requests[0].callback(fpromise::ok());

  EXPECT_EQ(ZX_OK, connection.status);
  EXPECT_TRUE(connection.socket.is_valid());
}

TEST_F(HostVsockEndpointTest, ConnectGuestToHostNoAcceptor) {
  TestConnectorConnection connection;
  host_endpoint_.Connect(kGuestCid, 1022, fuchsia::virtualization::HOST_CID, 22,
                         connection.callback());

  EXPECT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  EXPECT_FALSE(connection.socket.is_valid());
}

TEST_F(HostVsockEndpointTest, ListenMultipleTimesSamePort) {
  TestEndpointConnection connection;

  // Listen on port 22.
  TestHostVsockAcceptor host_acceptor1;
  host_endpoint_.Listen(22, host_acceptor1.NewBinding(), connection.listen_callback());

  EXPECT_EQ(ZX_OK, connection.status);

  // Listen again on port 22 and verify that it fails.
  TestHostVsockAcceptor host_acceptor2;
  host_endpoint_.Listen(22, host_acceptor2.NewBinding(), connection.listen_callback());

  EXPECT_EQ(ZX_ERR_ALREADY_BOUND, connection.status);
}

TEST_F(HostVsockEndpointTest, ConnectHostToGuestMultipleTimes) {
  constexpr size_t kNumTimes = 4;

  zx::socket handles[kNumTimes];
  for (size_t i = 0; i < kNumTimes; i++) {
    zx::socket* h = &handles[i];
    auto callback = [h](HostVsockEndpoint_Connect2_Result result) {
      *h = std::move(result.response().socket);
    };

    host_endpoint_.Connect2(22, std::move(callback));
  }

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(kNumTimes, requests.size());
  uint32_t port = kFirstEphemeralPort;
  for (const auto& request : requests) {
    EXPECT_EQ(fuchsia::virtualization::HOST_CID, request.src_cid);
    EXPECT_EQ(port++, request.src_port);
    EXPECT_EQ(22u, request.port);
    EXPECT_TRUE(request.socket.is_valid());
  }
}

// Open a connection from the host to the given guest on the given port.
zx::socket OpenConnectionToGuest(HostVsockEndpoint* host_endpoint, uint32_t port) {
  zx::socket host_end;
  auto callback = [&host_end](HostVsockEndpoint_Connect2_Result result) {
    host_end = std::move(result.response().socket);
  };

  host_endpoint->Connect2(port, std::move(callback));
  return host_end;
}

TEST_F(HostVsockEndpointTest, ConnectHostToGuestFreeEphemeralPort) {
  // Open two connections.
  zx::socket first = OpenConnectionToGuest(&host_endpoint_, /*port=*/22);
  zx::socket second = OpenConnectionToGuest(&host_endpoint_, /*port=*/22);
  RunLoopUntilIdle();

  // Ensure the two connections succeeded, and were allocated different ports.
  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_EQ(requests[0].src_port, kFirstEphemeralPort);
  EXPECT_EQ(requests[1].src_port, kFirstEphemeralPort + 1);

  // Disconnect the first connection and generate the shutdown event.
  first.reset();
  host_endpoint_.OnShutdown(requests[0].src_port);
  RunLoopUntilIdle();

  // Connect again. We expect the recently freed port to be under quarantine,
  // and should not be reallocated.
  zx::socket third = OpenConnectionToGuest(&host_endpoint_, /*port=*/22);
  requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(requests[0].src_port, kFirstEphemeralPort + 2);

  // Disconnect again, and wait for all quarantine periods to end.
  third.reset();
  host_endpoint_.OnShutdown(requests[0].src_port);
  RunLoopFor(kPortQuarantineTime * 2);

  // Connect a fourth time. This time, the ephemeral port should be reused.
  zx::socket fourth = OpenConnectionToGuest(&host_endpoint_, /*port=*/22);
  requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(requests[0].src_port, kFirstEphemeralPort);
}

}  // namespace
