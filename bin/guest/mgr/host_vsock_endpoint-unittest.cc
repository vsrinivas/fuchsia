// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"

#include <unordered_map>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/gtest/test_loop_fixture.h>

#include "garnet/bin/guest/mgr/guest_vsock_endpoint.h"

namespace guestmgr {
namespace {

static constexpr uint32_t kGuestCid = 3;
static constexpr uint32_t kOtherGuestCid = 4;

template <typename T>
class TestVsockAcceptorBase : public T {
 public:
  struct ConnectionRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    zx::handle handle;
    typename T::AcceptCallback callback;
  };

  fidl::InterfaceHandle<T> NewBinding() { return binding_.NewBinding(); }

  std::vector<ConnectionRequest> TakeRequests() {
    return std::move(connection_requests_);
  }

 protected:
  fidl::Binding<T> binding_{this};
  std::vector<ConnectionRequest> connection_requests_;
};

class TestVsockAcceptor
    : public TestVsockAcceptorBase<fuchsia::guest::GuestVsockAcceptor> {
 private:
  // |fuchsia::guest::GuestVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              zx::handle handle, AcceptCallback callback) override {
    connection_requests_.emplace_back(ConnectionRequest{
        src_cid, src_port, port, std::move(handle), std::move(callback)});
  }
};

class TestHostVsockAcceptor
    : public TestVsockAcceptorBase<fuchsia::guest::HostVsockAcceptor> {
 private:
  // |fuchsia::guest::HostVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override {
    connection_requests_.emplace_back(ConnectionRequest{
        src_cid, src_port, port, zx::handle(), std::move(callback)});
  }
};

struct TestConnectorConnection {
  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::handle handle;

  fuchsia::guest::HostVsockConnector::ConnectCallback callback() {
    return [this](zx_status_t status, zx::handle handle) {
      this->status = status;
      this->handle = std::move(handle);
    };
  }
};

struct TestEndpointConnection {
  zx_status_t status = ZX_ERR_BAD_STATE;

  fuchsia::guest::HostVsockEndpoint::ConnectCallback callback() {
    return [this](zx_status_t status) { this->status = status; };
  }
};

static void NoOpCallback(zx_status_t status) {}

class HostVsockEndpointTest : public ::gtest::TestLoopFixture {
 protected:
  HostVsockEndpoint host_endpoint_{
      fit::bind_member(this, &HostVsockEndpointTest::GetAcceptor)};
  TestVsockAcceptor guest_acceptor_;

 private:
  fuchsia::guest::GuestVsockAcceptor* GetAcceptor(uint32_t cid) {
    return cid == kGuestCid ? &guest_acceptor_ : nullptr;
  }
};

TEST_F(HostVsockEndpointTest, ConnectGuestToGuest) {
  TestConnectorConnection connection;
  host_endpoint_.Connect(kOtherGuestCid, 1022, kGuestCid, 22,
                         connection.callback());

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(kOtherGuestCid, requests[0].src_cid);
  EXPECT_EQ(1022u, requests[0].src_port);
  EXPECT_EQ(22u, requests[0].port);
  EXPECT_TRUE(requests[0].handle.is_valid());

  requests[0].callback(ZX_OK);

  EXPECT_EQ(ZX_OK, connection.status);
  EXPECT_TRUE(connection.handle.is_valid());
}

TEST_F(HostVsockEndpointTest, ConnectGuestToHost) {
  TestHostVsockAcceptor host_acceptor;
  host_endpoint_.Listen(22, host_acceptor.NewBinding(), NoOpCallback);

  TestConnectorConnection connection;
  host_endpoint_.Connect(kGuestCid, 1022, fuchsia::guest::kHostCid, 22,
                         connection.callback());

  RunLoopUntilIdle();

  auto requests = host_acceptor.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(kGuestCid, requests[0].src_cid);
  EXPECT_EQ(1022u, requests[0].src_port);
  EXPECT_EQ(22u, requests[0].port);

  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));

  requests[0].callback(ZX_OK, std::move(h1));

  RunLoopUntilIdle();

  EXPECT_EQ(ZX_OK, connection.status);
  EXPECT_TRUE(connection.handle.is_valid());
}

TEST_F(HostVsockEndpointTest, ConnectHostToGuest) {
  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));

  TestEndpointConnection connection;
  host_endpoint_.Connect(kGuestCid, 22, std::move(h1), connection.callback());

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(fuchsia::guest::kHostCid, requests[0].src_cid);
  EXPECT_EQ(kFirstEphemeralPort, requests[0].src_port);
  EXPECT_EQ(22u, requests[0].port);
  EXPECT_TRUE(requests[0].handle.is_valid());

  requests[0].callback(ZX_OK);

  EXPECT_EQ(ZX_OK, connection.status);
}

TEST_F(HostVsockEndpointTest, ConnectHostToHost) {
  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));

  TestEndpointConnection connection;
  host_endpoint_.Connect(fuchsia::guest::kHostCid, 22, std::move(h1),
                         connection.callback());

  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
}

TEST_F(HostVsockEndpointTest, ConnectGuestToGuestNoAcceptor) {
  TestConnectorConnection connection;
  host_endpoint_.Connect(kOtherGuestCid, 1022, kGuestCid + 1000, 22,
                         connection.callback());

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(0u, requests.size());

  EXPECT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  EXPECT_FALSE(connection.handle.is_valid());
}

TEST_F(HostVsockEndpointTest, ConnectGuestToHostNoAcceptor) {
  TestConnectorConnection connection;
  host_endpoint_.Connect(kGuestCid, 1022, fuchsia::guest::kHostCid, 22,
                         connection.callback());

  EXPECT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  EXPECT_FALSE(connection.handle.is_valid());
}

TEST_F(HostVsockEndpointTest, ConnectHostToGuestNoAcceptor) {
  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));

  TestEndpointConnection connection;
  host_endpoint_.Connect(kGuestCid + 1000, 22, std::move(h1),
                         connection.callback());

  EXPECT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
}

TEST_F(HostVsockEndpointTest, ListenMultipleTimesSamePort) {
  TestEndpointConnection connection;

  // Listen on port 22.
  TestHostVsockAcceptor host_acceptor1;
  host_endpoint_.Listen(22, host_acceptor1.NewBinding(), connection.callback());

  EXPECT_EQ(ZX_OK, connection.status);

  // Listen again on port 22 and verify that it fails.
  TestHostVsockAcceptor host_acceptor2;
  host_endpoint_.Listen(22, host_acceptor2.NewBinding(), connection.callback());

  EXPECT_EQ(ZX_ERR_ALREADY_BOUND, connection.status);
}

TEST_F(HostVsockEndpointTest, ConnectHostToGuestMultipleTimes) {
  constexpr size_t kNumTimes = 4;

  zx::socket handles[2 * kNumTimes];
  for (size_t i = 0; i < kNumTimes; i++) {
    zx::socket* h = &handles[i * 2];
    ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h[0], &h[1]));

    host_endpoint_.Connect(kGuestCid, 22, std::move(h[1]), NoOpCallback);
  }

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(kNumTimes, requests.size());
  uint32_t port = kFirstEphemeralPort;
  for (const auto& request : requests) {
    EXPECT_EQ(fuchsia::guest::kHostCid, request.src_cid);
    EXPECT_EQ(port++, request.src_port);
    EXPECT_EQ(22u, request.port);
    EXPECT_TRUE(request.handle.is_valid());
  }
}

TEST_F(HostVsockEndpointTest, ConnectHostToGuestFreeEphemeralPort) {
  // Connect twice.
  constexpr size_t kNumTimes = 2;

  zx::socket handles[2 * kNumTimes];
  for (size_t i = 0; i < kNumTimes; i++) {
    zx::socket* h = &handles[i * 2];
    ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h[0], &h[1]));

    host_endpoint_.Connect(kGuestCid, 22, std::move(h[1]), NoOpCallback);
  }

  auto requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(kNumTimes, requests.size());
  uint32_t port = kFirstEphemeralPort;
  for (const auto& request : requests) {
    EXPECT_EQ(fuchsia::guest::kHostCid, request.src_cid);
    EXPECT_EQ(port++, request.src_port);
    EXPECT_EQ(22u, request.port);
    EXPECT_TRUE(request.handle.is_valid());
    request.callback(ZX_OK);
  }

  // Disconnect the first connection.
  handles[0].reset();

  RunLoopUntilIdle();

  // Connect again and verify that the port is reused.
  ASSERT_EQ(ZX_OK,
            zx::socket::create(ZX_SOCKET_STREAM, &handles[0], &handles[1]));

  host_endpoint_.Connect(kGuestCid, 22, std::move(handles[1]), NoOpCallback);

  requests = guest_acceptor_.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  EXPECT_EQ(fuchsia::guest::kHostCid, requests[0].src_cid);
  EXPECT_EQ(kFirstEphemeralPort, requests[0].src_port);
  EXPECT_EQ(22u, requests[0].port);
  EXPECT_TRUE(requests[0].handle.is_valid());
}

}  // namespace
}  // namespace guestmgr
