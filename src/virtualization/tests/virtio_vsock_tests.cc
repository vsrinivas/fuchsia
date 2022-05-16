// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <future>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "guest_test.h"
#include "src/virtualization/tests/enclosed_guest.h"

namespace {

using ::fuchsia::virtualization::HostVsockAcceptor_Accept_Response;
using ::fuchsia::virtualization::HostVsockAcceptor_Accept_Result;
using ::fuchsia::virtualization::HostVsockEndpoint_Connect_Result;
using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;

template <class T>
class VsockGuestTest : public GuestTest<T>, public fuchsia::virtualization::HostVsockAcceptor {
 public:
  struct IncomingRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    fuchsia::virtualization::HostVsockAcceptor::AcceptCallback callback;
  };

  std::vector<IncomingRequest> requests;

  std::mutex mutex_;
  std::condition_variable cv_;
  bool listen_complete_ = false;

  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override {
    requests.push_back({src_cid, src_port, port, std::move(callback)});
  }

  void Read(zx::socket& socket, size_t amount) {
    size_t total_read = 0;
    while (total_read != amount) {
      size_t read;
      char buf[1000];
      zx_signals_t pending;
      socket.wait_one(ZX_SOCKET_READABLE, zx::time::infinite(), &pending);
      ASSERT_TRUE((pending & ZX_SOCKET_READABLE) != 0);
      size_t read_amount = std::min(amount - total_read, sizeof(buf));
      ASSERT_EQ(socket.read(0, &buf, read_amount, &read), ZX_OK);
      total_read += read;
    }
  }

  void TestReadWrite(zx::socket& socket) {
    Read(socket, 60000 * 4);
    uint8_t value = 42;
    size_t actual;
    ASSERT_EQ(socket.write(0, &value, 1, &actual), ZX_OK);
    EXPECT_EQ(static_cast<int>(actual), 1);
  }

  void TestThread() {
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

    fuchsia::virtualization::HostVsockEndpointSyncPtr vsock_endpoint;
    this->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

    fidl::Binding<fuchsia::virtualization::HostVsockAcceptor> binding{this};

    HostVsockEndpoint_Listen_Result listen_result;
    ASSERT_EQ(vsock_endpoint->Listen(8000, binding.NewBinding(), &listen_result), ZX_OK);
    ASSERT_TRUE(listen_result.is_response());
    {
      std::unique_lock lock(mutex_);
      listen_complete_ = true;
    }
    cv_.notify_one();
    ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
    ASSERT_TRUE(requests.size() == 1);
    ASSERT_EQ(requests[0].src_cid, this->GetGuestCid());
    ASSERT_EQ(requests[0].src_port, 49152u);
    ASSERT_EQ(requests[0].port, 8000u);
    zx::socket socket1, socket2;
    ASSERT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    requests.back().callback(HostVsockAcceptor_Accept_Result::WithResponse(
        HostVsockAcceptor_Accept_Response(std::move(socket2))));
    requests.pop_back();
    TestReadWrite(socket1);
    zx_signals_t pending;
    // Read the read/write completes we expect the util to close the connection.
    socket1.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), &pending);
    ASSERT_TRUE((pending & ZX_SOCKET_PEER_CLOSED) != 0);
    socket1.reset();
    // Attempt to connect into the guest
    EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    HostVsockEndpoint_Connect_Result connect_result;
    ASSERT_EQ(
        vsock_endpoint->Connect(this->GetGuestCid(), 8001, std::move(socket2), &connect_result),
        ZX_OK);
    ASSERT_TRUE(connect_result.is_response());

    // Wait for all the data
    TestReadWrite(socket1);
    // Close the connection by dropping the socket.
    socket1.reset();
    // Open another connection.
    EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    ASSERT_EQ(
        vsock_endpoint->Connect(this->GetGuestCid(), 8002, std::move(socket2), &connect_result),
        ZX_OK);
    ASSERT_TRUE(connect_result.is_response());
    // Read some data then close the connection
    Read(socket1, 10);
    socket1.reset();
    // Wait for another connection to get accepted
    EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    ASSERT_EQ(
        vsock_endpoint->Connect(this->GetGuestCid(), 8003, std::move(socket2), &connect_result),
        ZX_OK);
    ASSERT_TRUE(connect_result.is_response());
    // Keep writing until we get peer closed
    zx_status_t status;
    do {
      socket1.wait_one(ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_WRITABLE, zx::time::infinite(), &pending);
      if ((pending & ZX_SOCKET_WRITABLE) != 0) {
        uint8_t buf[1000] = {};
        size_t actual = 0;
        status = socket1.write(0, buf, sizeof(buf), &actual);
      }
    } while (status != ZX_ERR_PEER_CLOSED && (pending & ZX_SOCKET_PEER_CLOSED) == 0);
  }
};

// TODO(fxbug.dev/86054) Presently vsock tests are not run on Zircon as the zircon guest vsock
// driver has known bugs that need fixing.
using GuestTypes = ::testing::Types<DebianEnclosedGuest, TerminaEnclosedGuest>;

TYPED_TEST_SUITE(VsockGuestTest, GuestTypes, GuestTestNameGenerator);

TYPED_TEST(VsockGuestTest, ConnectDisconnect) {
  auto handle = std::async(std::launch::async, [this] { this->TestThread(); });
  {
    std::unique_lock lock(this->mutex_);
    this->cv_.wait(lock, [&] { return this->listen_complete_; });
  }
  std::string result;
  EXPECT_EQ(this->RunUtil("virtio_vsock_test_util", {}, &result), ZX_OK);

  handle.wait();

  EXPECT_THAT(result, ::testing::HasSubstr("PASS"));
}

}  // namespace
