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
using ::fuchsia::virtualization::HostVsockEndpoint_Connect2_Result;
using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;

template <class T>
class VsockGuestTest : public GuestTest<T>, public fuchsia::virtualization::HostVsockAcceptor {
 public:
  void SetUp() override {
    GuestTest<T>::SetUp();
    this->GetHostVsockEndpoint(vsock_endpoint_.NewRequest());

    // An initial listener must be set up before the test util starts.
    HostVsockEndpoint_Listen_Result listen_result;
    ASSERT_EQ(vsock_endpoint_->Listen(8000, binding_.NewBinding(), &listen_result), ZX_OK);
    ASSERT_TRUE(listen_result.is_response());
  }

  void TestListen() {
    ASSERT_EQ(binding_.WaitForMessage(), ZX_OK);
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
  }

  void TestBasicReadWrite() {
    HostVsockEndpoint_Connect2_Result result;
    ASSERT_EQ(vsock_endpoint_->Connect2(8001, &result), ZX_OK);
    ASSERT_TRUE(result.is_response());
    zx::socket socket = std::move(result.response().socket);
    ASSERT_TRUE(socket.is_valid());

    TestReadWrite(socket);
    socket.reset();
  }

  void TestRead() {
    HostVsockEndpoint_Connect2_Result result;
    ASSERT_EQ(vsock_endpoint_->Connect2(8002, &result), ZX_OK);
    ASSERT_TRUE(result.is_response());
    zx::socket socket = std::move(result.response().socket);
    ASSERT_TRUE(socket.is_valid());

    Read(socket, 10);
    socket.reset();
  }

  void TestWrite() {
    HostVsockEndpoint_Connect2_Result result;
    ASSERT_EQ(vsock_endpoint_->Connect2(8003, &result), ZX_OK);
    ASSERT_TRUE(result.is_response());
    zx::socket socket = std::move(result.response().socket);
    ASSERT_TRUE(socket.is_valid());

    // Keep writing until we get peer closed
    zx_status_t status;
    zx_signals_t pending;
    do {
      socket.wait_one(ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_WRITABLE, zx::time::infinite(), &pending);
      if ((pending & ZX_SOCKET_WRITABLE) != 0) {
        uint8_t buf[1000] = {};
        size_t actual = 0;
        status = socket.write(0, buf, sizeof(buf), &actual);
      }
    } while (status != ZX_ERR_PEER_CLOSED && (pending & ZX_SOCKET_PEER_CLOSED) == 0);
  }

 private:
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

  struct IncomingRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    fuchsia::virtualization::HostVsockAcceptor::AcceptCallback callback;
  };

  std::vector<IncomingRequest> requests;
  fuchsia::virtualization::HostVsockEndpointSyncPtr vsock_endpoint_;
  fidl::Binding<fuchsia::virtualization::HostVsockAcceptor> binding_{this};
};

// TODO(fxbug.dev/86054) Presently vsock tests are not run on Zircon as the zircon guest vsock
// driver has known bugs that need fixing.
using GuestTypes = ::testing::Types<DebianEnclosedGuest, TerminaEnclosedGuest>;

TYPED_TEST_SUITE(VsockGuestTest, GuestTypes, GuestTestNameGenerator);

TYPED_TEST(VsockGuestTest, ConnectDisconnect) {
  std::string result;
  auto handle = std::async(std::launch::async, [this, &result] {
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    return this->RunUtil("virtio_vsock_test_util", {"integration_test"}, &result);
  });

  this->TestListen();
  this->TestBasicReadWrite();
  this->TestRead();
  this->TestWrite();

  ASSERT_EQ(ZX_OK, handle.get());
  EXPECT_THAT(result, ::testing::HasSubstr("PASS"));
}

}  // namespace
