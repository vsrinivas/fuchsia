// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string.h>
#include <future>

#include "guest_test.h"

template <class T>
T* GuestTest<T>::enclosed_guest_ = nullptr;

template <class T>
class VsockGuestTest : public GuestTest<T>,
                       public fuchsia::virtualization::HostVsockAcceptor {
 public:
  struct IncomingRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    fuchsia::virtualization::HostVsockAcceptor::AcceptCallback callback;
  };

  std::vector<IncomingRequest> requests;

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
    async::Loop loop(&kAsyncLoopConfigAttachToThread);

    fuchsia::virtualization::HostVsockEndpointSyncPtr vsock_endpoint;
    this->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

    fidl::Binding<fuchsia::virtualization::HostVsockAcceptor> binding{this};
    zx_status_t out_status;
    ASSERT_EQ(vsock_endpoint->Listen(8000, binding.NewBinding(), &out_status),
              ZX_OK);
    ASSERT_EQ(out_status, ZX_OK);

    ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
    ASSERT_TRUE(requests.size() == 1);
    ASSERT_EQ(requests[0].src_cid, this->GetGuestCid());
    ASSERT_EQ(requests[0].src_port, 49152u);
    ASSERT_EQ(requests[0].port, 8000u);

    zx::socket socket1, socket2;
    ASSERT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    requests.back().callback(ZX_OK, std::move(socket2));
    requests.pop_back();
    TestReadWrite(socket1);

    zx_signals_t pending;
    // Read the read/write completes we expect the util to close the connection.
    socket1.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), &pending);
    ASSERT_TRUE((pending & ZX_SOCKET_PEER_CLOSED) != 0);

    socket1.reset();

    // Attempt to connect into the guest
    EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    ASSERT_EQ(vsock_endpoint->Connect(this->GetGuestCid(), 8001,
                                      std::move(socket2), &out_status),
              ZX_OK);
    ASSERT_EQ(out_status, ZX_OK);

    // Wait for all the data
    TestReadWrite(socket1);
    // Close the connection by dropping the socket.
    socket1.reset();

    // Open another connection.
    EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    ASSERT_EQ(vsock_endpoint->Connect(this->GetGuestCid(), 8002,
                                      std::move(socket2), &out_status),
              ZX_OK);
    ASSERT_EQ(out_status, ZX_OK);
    // Read some data then close the connection
    Read(socket1, 10);
    socket1.reset();

    // Wait for another connection to get accepted
    EXPECT_EQ(zx::socket::create(ZX_SOCKET_STREAM, &socket1, &socket2), ZX_OK);
    ASSERT_EQ(vsock_endpoint->Connect(this->GetGuestCid(), 8003,
                                      std::move(socket2), &out_status),
              ZX_OK);
    ASSERT_EQ(out_status, ZX_OK);
    // Keep writing until we get peer closed
    zx_status_t status;
    do {
      socket1.wait_one(ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_WRITABLE,
                       zx::time::infinite(), &pending);
      if ((pending & ZX_SOCKET_WRITABLE) != 0) {
        uint8_t buf[1000] = {};
        size_t actual = 0;
        status = socket1.write(0, buf, sizeof(buf), &actual);
      }
    } while (status != ZX_ERR_PEER_CLOSED &&
             (pending & ZX_SOCKET_PEER_CLOSED) == 0);
  }
};

using GuestTypes = ::testing::Types<ZirconEnclosedGuest, DebianEnclosedGuest>;

TYPED_TEST_SUITE(VsockGuestTest, GuestTypes);

TYPED_TEST(VsockGuestTest, ConnectDisconnect) {
  auto handle = std::async(std::launch::async, [this] { this->TestThread(); });

  std::string result;
  EXPECT_EQ(this->RunUtil("virtio_vsock_test_util", {}, &result), ZX_OK);

  handle.wait();

  EXPECT_THAT(result, ::testing::HasSubstr("PASS"));
}
