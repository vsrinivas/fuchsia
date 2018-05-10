// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

namespace machina {
namespace {

static constexpr size_t kDataSize = 4;
static constexpr uint32_t kVirtioVsockHostPort = 22;
static constexpr uint32_t kVirtioVsockGuestCid = 3;
static constexpr uint32_t kVirtioVsockGuestPort = 23;
static constexpr uint32_t kVirtioVsockGuestEphemeralPort = 1024;
static constexpr uint16_t kVirtioVsockQueueSize = 32;

struct ConnectionRequest {
  uint32_t src_port;
  uint32_t cid;
  uint32_t port;
  guest::SocketConnector::ConnectCallback callback;
};

struct TestConnection {
  uint32_t count = 0;
  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::socket socket;

  guest::SocketAcceptor::AcceptCallback callback() {
    return [this](zx_status_t status, zx::socket socket) {
      count++;
      this->status = status;
      this->socket = std::move(socket);
    };
  }
};

class VirtioVsockTest : public testing::Test, public guest::SocketConnector {
 public:
  VirtioVsockTest()
      : vsock_(nullptr, phys_mem_, loop_.async()),
        rx_queue_(vsock_.rx_queue()),
        tx_queue_(vsock_.tx_queue()) {}

  void SetUp() override {
    ASSERT_EQ(rx_queue_.Init(kVirtioVsockQueueSize), ZX_OK);
    ASSERT_EQ(tx_queue_.Init(kVirtioVsockQueueSize), ZX_OK);
    ASSERT_EQ(endpoint_binding_.Bind(endpoint_.NewRequest()), ZX_OK);
    endpoint_->SetContextId(kVirtioVsockGuestCid,
                            connector_binding_.NewBinding(),
                            acceptor_.NewRequest());
    loop_.RunUntilIdle();
  }

 protected:
  PhysMemFake phys_mem_;
  async::Loop loop_{&kAsyncLoopConfigMakeDefault};
  VirtioVsock vsock_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  fidl::Binding<guest::SocketEndpoint> endpoint_binding_{&vsock_};
  guest::SocketEndpointPtr endpoint_;
  guest::SocketAcceptorPtr acceptor_;
  fidl::Binding<guest::SocketConnector> connector_binding_{this};
  std::vector<zx::socket> remote_sockets_;
  std::vector<ConnectionRequest> connection_requests_;
  std::vector<ConnectionRequest> connections_established_;

  // |guest::SocketConnector|
  void Connect(uint32_t src_port, uint32_t cid, uint32_t port,
               guest::SocketConnector::ConnectCallback callback) override {
    connection_requests_.emplace_back(
        ConnectionRequest{src_port, cid, port, std::move(callback)});
  }

  void VerifyHeader(virtio_vsock_hdr_t* header, uint32_t host_port,
                    uint32_t guest_port, uint32_t len, uint16_t op,
                    uint32_t flags) {
    EXPECT_EQ(header->src_cid, guest::kHostCid);
    EXPECT_EQ(header->dst_cid, kVirtioVsockGuestCid);
    EXPECT_EQ(header->src_port, host_port);
    EXPECT_EQ(header->dst_port, guest_port);
    EXPECT_EQ(header->len, len);
    EXPECT_EQ(header->type, VIRTIO_VSOCK_TYPE_STREAM);
    EXPECT_EQ(header->op, op);
    EXPECT_EQ(header->flags, flags);
  }

  void DoReceive(virtio_vsock_hdr_t* rx_header, size_t rx_size) {
    ASSERT_EQ(
        rx_queue_.BuildDescriptor().AppendWritable(rx_header, rx_size).Build(),
        ZX_OK);

    loop_.RunUntilIdle();
  }

  void DoSend(uint32_t host_port, uint32_t guest_port, uint16_t type,
              uint16_t op) {
    virtio_vsock_hdr_t tx_header = {
        .src_cid = kVirtioVsockGuestCid,
        .dst_cid = guest::kHostCid,
        .src_port = guest_port,
        .dst_port = host_port,
        .type = type,
        .op = op,
    };
    ASSERT_EQ(tx_queue_.BuildDescriptor()
                  .AppendReadable(&tx_header, sizeof(tx_header))
                  .Build(),
              ZX_OK);

    loop_.RunUntilIdle();
  }

  void HostConnectOnPortRequest(
      uint32_t host_port, guest::SocketAcceptor::AcceptCallback callback) {
    acceptor_->Accept(guest::kHostCid, host_port, kVirtioVsockGuestPort,
                      std::move(callback));

    virtio_vsock_hdr_t rx_header = {};
    DoReceive(&rx_header, sizeof(rx_header));
    VerifyHeader(&rx_header, host_port, kVirtioVsockGuestPort, 0,
                 VIRTIO_VSOCK_OP_REQUEST, 0);
  }

  void HostConnectOnPortResponse(uint32_t host_port) {
    DoSend(host_port, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
           VIRTIO_VSOCK_OP_RESPONSE);
  }

  void HostReadOnPort(uint32_t host_port, zx::socket* socket) {
    uint8_t expected_data[kDataSize] = {1, 9, 8, 5};
    size_t actual;
    ASSERT_EQ(socket->write(0, expected_data, sizeof(expected_data), &actual),
              ZX_OK);
    EXPECT_EQ(actual, kDataSize);

    uint8_t rx_buffer[sizeof(virtio_vsock_hdr_t) + kDataSize] = {};
    auto rx_header = reinterpret_cast<virtio_vsock_hdr_t*>(rx_buffer);
    DoReceive(rx_header, sizeof(rx_buffer));
    VerifyHeader(rx_header, host_port, kVirtioVsockGuestPort, 4,
                 VIRTIO_VSOCK_OP_RW, 0);

    auto rx_data = rx_buffer + sizeof(*rx_header);
    EXPECT_EQ(memcmp(rx_data, expected_data, kDataSize), 0);
  }

  void HostWriteOnPort(uint32_t host_port, zx::socket* socket) {
    uint8_t tx_buffer[sizeof(virtio_vsock_hdr_t) + kDataSize] = {};
    auto tx_header = reinterpret_cast<virtio_vsock_hdr_t*>(tx_buffer);
    *tx_header = {
        .src_cid = kVirtioVsockGuestCid,
        .dst_cid = guest::kHostCid,
        .src_port = kVirtioVsockGuestPort,
        .dst_port = host_port,
        .len = kDataSize,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = VIRTIO_VSOCK_OP_RW,
    };

    uint8_t expected_data[kDataSize] = {2, 3, 0, 1};
    auto tx_data = tx_buffer + sizeof(*tx_header);
    memcpy(tx_data, expected_data, kDataSize);
    ASSERT_EQ(tx_queue_.BuildDescriptor()
                  .AppendReadable(tx_buffer, sizeof(tx_buffer))
                  .Build(),
              ZX_OK);

    loop_.RunUntilIdle();

    uint8_t actual_data[kDataSize];
    size_t actual;
    ASSERT_EQ(socket->read(0, actual_data, sizeof(actual_data), &actual),
              ZX_OK);
    EXPECT_EQ(actual, kDataSize);
    EXPECT_EQ(memcmp(actual_data, expected_data, kDataSize), 0);
  }

  void HostConnectOnPort(uint32_t host_port) {
    TestConnection connection;
    HostConnectOnPortRequest(host_port, connection.callback());
    HostConnectOnPortResponse(host_port);
    loop_.RunUntilIdle();
    ASSERT_EQ(1u, connection.count);
    ASSERT_EQ(ZX_OK, connection.status);
    ASSERT_TRUE(connection.socket.is_valid());
    HostReadOnPort(host_port, &connection.socket);
  }

  void HostShutdownOnPort(uint32_t host_port, uint32_t flags) {
    virtio_vsock_hdr_t rx_header = {};
    DoReceive(&rx_header, sizeof(rx_header));
    VerifyHeader(&rx_header, host_port, kVirtioVsockGuestPort, 0,
                 VIRTIO_VSOCK_OP_SHUTDOWN, flags);
  }

  void GuestConnectOnPortRequest(uint32_t host_port, uint32_t guest_port) {
    DoSend(host_port, guest_port, VIRTIO_VSOCK_TYPE_STREAM,
           VIRTIO_VSOCK_OP_REQUEST);
    loop_.RunUntilIdle();
  }

  void GuestConnectInvokeCallbacks(zx_status_t status = ZX_OK) {
    for (auto it = connection_requests_.begin();
         it != connection_requests_.end();
         it = connection_requests_.erase(it)) {
      zx::socket h1, h2;
      if (status == ZX_OK) {
        ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));
        remote_sockets_.emplace_back(std::move(h2));
      }
      it->callback(status, std::move(h1));
      connections_established_.emplace_back(
          ConnectionRequest{it->src_port, it->cid, it->port, nullptr});
      loop_.RunUntilIdle();
    }
  }

  void GuestConnectOnPortResponse(uint32_t host_port, uint16_t op,
                                  uint32_t guest_port) {
    virtio_vsock_hdr_t rx_header = {};
    DoReceive(&rx_header, sizeof(rx_header));
    VerifyHeader(&rx_header, host_port, guest_port, 0, op, 0);
    if (op == VIRTIO_VSOCK_OP_RST) {
      EXPECT_EQ(rx_header.buf_alloc, 0u);
    } else {
      EXPECT_GT(rx_header.buf_alloc, 0u);
    }
    EXPECT_EQ(rx_header.fwd_cnt, 0u);
  }

  void GuestConnectOnPort(
      uint32_t host_port,
      uint32_t guest_port = kVirtioVsockGuestEphemeralPort) {
    GuestConnectOnPortRequest(host_port, guest_port);
    GuestConnectInvokeCallbacks();
    GuestConnectOnPortResponse(host_port, VIRTIO_VSOCK_OP_RESPONSE, guest_port);
  }
};

TEST_F(VirtioVsockTest, Connect) { HostConnectOnPort(kVirtioVsockHostPort); }

TEST_F(VirtioVsockTest, ConnectMultipleTimes) {
  HostConnectOnPort(kVirtioVsockHostPort);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
  HostConnectOnPort(kVirtioVsockHostPort + 1000);
}

TEST_F(VirtioVsockTest, ConnectMultipleTimesSamePort) {
  HostConnectOnPort(kVirtioVsockHostPort);

  TestConnection connection;
  acceptor_->Accept(guest::kHostCid, kVirtioVsockHostPort,
                    kVirtioVsockGuestPort, connection.callback());
  loop_.RunUntilIdle();
  ASSERT_EQ(1u, connection.count);
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, connection.status);
  ASSERT_FALSE(connection.socket.is_valid());
}

TEST_F(VirtioVsockTest, ConnectRefused) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());

  // Test connection reset.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
         VIRTIO_VSOCK_OP_RST);
  loop_.RunUntilIdle();
  ASSERT_EQ(1u, connection.count);
  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  ASSERT_FALSE(connection.socket.is_valid());
  EXPECT_FALSE(vsock_.HasConnection(guest::kHostCid, kVirtioVsockHostPort,
                                    kVirtioVsockGuestPort));
}

TEST_F(VirtioVsockTest, Listen) {
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort);
  ASSERT_EQ(1u, connections_established_.size());
  ASSERT_TRUE(vsock_.HasConnection(guest::kHostCid, kVirtioVsockHostPort,
                                   kVirtioVsockGuestEphemeralPort));
}

TEST_F(VirtioVsockTest, ListenMultipleTimes) {
  GuestConnectOnPort(kVirtioVsockHostPort + 1,
                     kVirtioVsockGuestEphemeralPort + 1);
  GuestConnectOnPort(kVirtioVsockHostPort + 2,
                     kVirtioVsockGuestEphemeralPort + 2);
  ASSERT_EQ(2u, connections_established_.size());
  ASSERT_TRUE(vsock_.HasConnection(guest::kHostCid, kVirtioVsockHostPort + 1,
                                   kVirtioVsockGuestEphemeralPort + 1));
  ASSERT_TRUE(vsock_.HasConnection(guest::kHostCid, kVirtioVsockHostPort + 2,
                                   kVirtioVsockGuestEphemeralPort + 2));
}

TEST_F(VirtioVsockTest, ListenMultipleTimesSamePort) {
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort);
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort + 1);

  EXPECT_TRUE(vsock_.HasConnection(guest::kHostCid, kVirtioVsockHostPort,
                                   kVirtioVsockGuestEphemeralPort));
  EXPECT_TRUE(vsock_.HasConnection(guest::kHostCid, kVirtioVsockHostPort,
                                   kVirtioVsockGuestEphemeralPort + 1));
}

TEST_F(VirtioVsockTest, ListenRefused) {
  GuestConnectOnPortRequest(kVirtioVsockHostPort,
                            kVirtioVsockGuestEphemeralPort);
  GuestConnectInvokeCallbacks(ZX_ERR_CONNECTION_REFUSED);
  GuestConnectOnPortResponse(kVirtioVsockHostPort, VIRTIO_VSOCK_OP_RST,
                             kVirtioVsockGuestEphemeralPort);
  EXPECT_FALSE(vsock_.HasConnection(guest::kHostCid, kVirtioVsockHostPort,
                                    kVirtioVsockGuestEphemeralPort));
}

TEST_F(VirtioVsockTest, Reset) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  connection.socket.reset();
  loop_.RunUntilIdle();
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
}

TEST_F(VirtioVsockTest, ShutdownRead) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(
      connection.socket.write(ZX_SOCKET_SHUTDOWN_WRITE, nullptr, 0, nullptr),
      ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV);
}

TEST_F(VirtioVsockTest, ShutdownWrite) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(
      connection.socket.write(ZX_SOCKET_SHUTDOWN_READ, nullptr, 0, nullptr),
      ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);
}

TEST_F(VirtioVsockTest, WriteAfterShutdown) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(
      connection.socket.write(ZX_SOCKET_SHUTDOWN_READ, nullptr, 0, nullptr),
      ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);

  // Test write after shutdown.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
         VIRTIO_VSOCK_OP_RW);
  virtio_vsock_hdr_t rx_header = {};
  DoReceive(&rx_header, sizeof(rx_header));
  VerifyHeader(&rx_header, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0,
               VIRTIO_VSOCK_OP_RST, 0);
}

TEST_F(VirtioVsockTest, Read) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostReadOnPort(kVirtioVsockHostPort, &connection.socket);
  HostReadOnPort(kVirtioVsockHostPort, &connection.socket);
}

TEST_F(VirtioVsockTest, Write) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostWriteOnPort(kVirtioVsockHostPort, &connection.socket);
  HostWriteOnPort(kVirtioVsockHostPort, &connection.socket);
}

TEST_F(VirtioVsockTest, MultipleConnections) {
  TestConnection a_connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort + 1000,
                           a_connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort + 1000);

  TestConnection b_connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort + 2000,
                           b_connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort + 2000);

  for (auto i = 0; i < (kVirtioVsockQueueSize / 4); i++) {
    HostReadOnPort(kVirtioVsockHostPort + 1000, &a_connection.socket);
    HostReadOnPort(kVirtioVsockHostPort + 2000, &b_connection.socket);
    HostWriteOnPort(kVirtioVsockHostPort + 1000, &a_connection.socket);
    HostWriteOnPort(kVirtioVsockHostPort + 2000, &b_connection.socket);
  }
}

TEST_F(VirtioVsockTest, CreditRequest) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, connection.callback());
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  // Test credit request.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
         VIRTIO_VSOCK_OP_CREDIT_REQUEST);

  virtio_vsock_hdr_t rx_header = {};
  DoReceive(&rx_header, sizeof(rx_header));
  VerifyHeader(&rx_header, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0,
               VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0);
  EXPECT_GT(rx_header.buf_alloc, 0u);
  EXPECT_EQ(rx_header.fwd_cnt, 0u);
}

TEST_F(VirtioVsockTest, UnsupportedSocketType) {
  // Test connection request with invalid type.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, UINT16_MAX,
         VIRTIO_VSOCK_OP_REQUEST);

  virtio_vsock_hdr_t rx_header = {};
  DoReceive(&rx_header, sizeof(rx_header));
  EXPECT_EQ(rx_header.src_cid, guest::kHostCid);
  EXPECT_EQ(rx_header.dst_cid, kVirtioVsockGuestCid);
  EXPECT_EQ(rx_header.src_port, kVirtioVsockHostPort);
  EXPECT_EQ(rx_header.dst_port, kVirtioVsockGuestPort);
  EXPECT_EQ(rx_header.type, VIRTIO_VSOCK_TYPE_STREAM);
  EXPECT_EQ(rx_header.op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(rx_header.flags, 0u);
}

}  // namespace
}  // namespace machina
