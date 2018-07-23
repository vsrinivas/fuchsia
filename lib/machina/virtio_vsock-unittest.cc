// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "lib/gtest/test_loop_fixture.h"

namespace machina {
namespace {

static constexpr size_t kDataSize = 4;

struct RxBuffer {
  // The number of virtio descriptors to use for this buffer.
  static constexpr size_t kNumDescriptors = 3;

  virtio_vsock_hdr_t header;
  uint8_t data[kDataSize] __ALIGNED(16);
  uint8_t data2[kDataSize] __ALIGNED(16);
};

// Ensure we have padding bytes in our structure so we don't have contiguous
// descriptors.
static_assert(offsetof(RxBuffer, data) > sizeof(RxBuffer::header),
              "RxBuffer::data is adjacent to RxBuffer::header");
static_assert(offsetof(RxBuffer, data2) >
                  offsetof(RxBuffer, data) + sizeof(RxBuffer::data),
              "RxBuffer::data2 is adjacent to RxBuffer::data");

static constexpr uint32_t kVirtioVsockHostPort = 22;
static constexpr uint32_t kVirtioVsockGuestCid = 3;
static constexpr uint32_t kVirtioVsockGuestPort = 23;
static constexpr uint32_t kVirtioVsockGuestEphemeralPort = 1024;
static constexpr uint16_t kVirtioVsockRxBuffers = 8;
static constexpr uint16_t kVirtioVsockQueueSize =
    kVirtioVsockRxBuffers * RxBuffer::kNumDescriptors;
static const std::vector<uint8_t> kDefaultData = {1, 9, 8, 5};

struct ConnectionRequest {
  uint32_t src_cid;
  uint32_t src_port;
  uint32_t cid;
  uint32_t port;
  fuchsia::guest::HostVsockConnector::ConnectCallback callback;
};

struct TestConnection {
  uint32_t count = 0;
  zx_status_t status = ZX_ERR_BAD_STATE;
  zx::socket socket;
  zx::socket remote_socket;

  TestConnection() {
    FXL_CHECK(zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket) ==
              ZX_OK);
  }

  fuchsia::guest::GuestVsockAcceptor::AcceptCallback callback() {
    return [this](zx_status_t st) {
      count++;
      status = st;
    };
  }

  bool remote_closed() const {
    zx_signals_t observed = 0;
    zx_status_t status =
        socket.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time(), &observed);
    switch (status) {
      case ZX_ERR_TIMED_OUT:
        return false;
      case ZX_OK:
        return observed & ZX_SOCKET_PEER_CLOSED;
      default:
        FXL_CHECK(false) << "Unexpected status " << status;
        __UNREACHABLE;
    }
  }
};

class VirtioVsockTest : public ::gtest::TestLoopFixture,
                        public fuchsia::guest::HostVsockConnector {
 public:
  VirtioVsockTest()
      : vsock_(nullptr, phys_mem_, dispatcher()),
        rx_queue_(vsock_.rx_queue()),
        tx_queue_(vsock_.tx_queue()) {}

  void SetUp() override {
    ASSERT_EQ(rx_queue_.Init(kVirtioVsockQueueSize), ZX_OK);
    ASSERT_EQ(tx_queue_.Init(kVirtioVsockQueueSize), ZX_OK);
    FillRxQueue();
    ASSERT_EQ(endpoint_binding_.Bind(endpoint_.NewRequest()), ZX_OK);
    endpoint_->SetContextId(kVirtioVsockGuestCid,
                            connector_binding_.NewBinding(),
                            acceptor_.NewRequest());
    RunLoopUntilIdle();
  }

 protected:
  PhysMemFake phys_mem_;
  VirtioVsock vsock_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  fidl::Binding<fuchsia::guest::GuestVsockEndpoint> endpoint_binding_{&vsock_};
  fuchsia::guest::GuestVsockEndpointPtr endpoint_;
  fuchsia::guest::GuestVsockAcceptorPtr acceptor_;
  fidl::Binding<fuchsia::guest::HostVsockConnector> connector_binding_{this};
  std::vector<zx::socket> remote_sockets_;
  std::vector<ConnectionRequest> connection_requests_;
  std::vector<ConnectionRequest> connections_established_;
  RxBuffer rx_buffers[kVirtioVsockRxBuffers] = {};

  // Set some default credit parameters that should suffice for most tests.
  // Tests of the credit system will want to assign a more reasonable buf_alloc
  // value.
  uint32_t buf_alloc = UINT32_MAX;
  uint32_t fwd_cnt = 0;

  // |fuchsia::guest::HostVsockConnector|
  void Connect(
      uint32_t src_cid, uint32_t src_port, uint32_t cid, uint32_t port,
      fuchsia::guest::HostVsockConnector::ConnectCallback callback) override {
    connection_requests_.emplace_back(
        ConnectionRequest{src_cid, src_port, cid, port, std::move(callback)});
  }

  void VerifyHeader(virtio_vsock_hdr_t* header, uint32_t host_port,
                    uint32_t guest_port, uint32_t len, uint16_t op,
                    uint32_t flags) {
    EXPECT_EQ(header->src_cid, fuchsia::guest::kHostCid);
    EXPECT_EQ(header->dst_cid, kVirtioVsockGuestCid);
    EXPECT_EQ(header->src_port, host_port);
    EXPECT_EQ(header->dst_port, guest_port);
    EXPECT_EQ(header->len, len);
    EXPECT_EQ(header->type, VIRTIO_VSOCK_TYPE_STREAM);
    EXPECT_EQ(header->op, op);
    EXPECT_EQ(header->flags, flags);
  }

  RxBuffer* DoReceive() {
    RunLoopUntilIdle();
    if (!rx_queue_.HasUsed()) {
      return nullptr;
    }
    vring_used_elem used_elem = rx_queue_.NextUsed();
    return &rx_buffers[used_elem.id / RxBuffer::kNumDescriptors];
  }

  void DoSend(uint32_t host_port, uint32_t guest_cid, uint32_t guest_port,
              uint16_t type, uint16_t op) {
    virtio_vsock_hdr_t tx_header = {
        .src_cid = guest_cid,
        .dst_cid = fuchsia::guest::kHostCid,
        .src_port = guest_port,
        .dst_port = host_port,
        .type = type,
        .op = op,
        .buf_alloc = this->buf_alloc,
        .fwd_cnt = this->fwd_cnt,
    };
    ASSERT_EQ(tx_queue_.BuildDescriptor()
                  .AppendReadable(&tx_header, sizeof(tx_header))
                  .Build(),
              ZX_OK);

    RunLoopUntilIdle();
  }

  void HostConnectOnPortRequest(uint32_t host_port,
                                TestConnection* connection) {
    acceptor_->Accept(
        fuchsia::guest::kHostCid, host_port, kVirtioVsockGuestPort,
        std::move(connection->remote_socket), connection->callback());

    RxBuffer* rx_buffer = DoReceive();
    ASSERT_NE(nullptr, rx_buffer);
    VerifyHeader(&rx_buffer->header, host_port, kVirtioVsockGuestPort, 0,
                 VIRTIO_VSOCK_OP_REQUEST, 0);
  }

  void HostConnectOnPortResponse(uint32_t host_port) {
    DoSend(host_port, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
           VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_RESPONSE);
  }

  void FillRxQueue() {
    for (size_t i = 0; i < countof(rx_buffers); ++i) {
      ASSERT_EQ(
          rx_queue_.BuildDescriptor()
              .AppendWritable(&rx_buffers[i].header,
                              sizeof(rx_buffers[i].header))
              .AppendWritable(&rx_buffers[i].data, sizeof(rx_buffers[i].data))
              .AppendWritable(&rx_buffers[i].data2, sizeof(rx_buffers[i].data2))
              .Build(),
          ZX_OK);
    }
  }

  void HostReadOnPort(uint32_t host_port, zx::socket* socket,
                      std::vector<uint8_t> expected = kDefaultData) {
    size_t actual;
    ASSERT_EQ(socket->write(0, expected.data(), expected.size(), &actual),
              ZX_OK);
    EXPECT_EQ(actual, expected.size());

    RxBuffer* rx_buffer = DoReceive();
    ASSERT_NE(nullptr, rx_buffer);
    VerifyHeader(&rx_buffer->header, host_port, kVirtioVsockGuestPort,
                 expected.size(), VIRTIO_VSOCK_OP_RW, 0);

    // Verify the data, which may be spread across multiple descriptors.
    EXPECT_EQ(memcmp(rx_buffer->data, expected.data(),
                     expected.size() > kDataSize ? kDataSize : expected.size()),
              0);
    if (expected.size() > kDataSize) {
      EXPECT_EQ(memcmp(rx_buffer->data2, expected.data() + kDataSize,
                       expected.size() - kDataSize),
                0);
    }
  }

  void HostQueueWriteOnPort(uint32_t host_port, uint8_t* tx_buffer,
                            size_t len) {
    auto tx_header = reinterpret_cast<virtio_vsock_hdr_t*>(tx_buffer);
    ASSERT_GE(len, sizeof(*tx_header));
    *tx_header = {
        .src_cid = kVirtioVsockGuestCid,
        .dst_cid = fuchsia::guest::kHostCid,
        .src_port = kVirtioVsockGuestPort,
        .dst_port = host_port,
        .len = static_cast<uint32_t>(len - sizeof(*tx_header)),
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = VIRTIO_VSOCK_OP_RW,
    };

    ASSERT_EQ(
        tx_queue_.BuildDescriptor().AppendReadable(tx_buffer, len).Build(),
        ZX_OK);
  }

  void HostWriteOnPort(uint32_t host_port, zx::socket* socket) {
    uint8_t tx_buffer[sizeof(virtio_vsock_hdr_t) + kDataSize] = {};
    uint8_t expected_data[kDataSize] = {2, 3, 0, 1};
    memcpy(tx_buffer + sizeof(virtio_vsock_hdr_t), expected_data, kDataSize);
    HostQueueWriteOnPort(host_port, tx_buffer, sizeof(tx_buffer));

    RunLoopUntilIdle();

    uint8_t actual_data[kDataSize];
    size_t actual;
    ASSERT_EQ(socket->read(0, actual_data, sizeof(actual_data), &actual),
              ZX_OK);
    EXPECT_EQ(actual, kDataSize);
    EXPECT_EQ(memcmp(actual_data, expected_data, kDataSize), 0);
  }

  void HostConnectOnPort(uint32_t host_port) {
    TestConnection connection;
    HostConnectOnPortRequest(host_port, &connection);
    HostConnectOnPortResponse(host_port);
    RunLoopUntilIdle();
    ASSERT_EQ(1u, connection.count);
    ASSERT_EQ(ZX_OK, connection.status);
    ASSERT_FALSE(connection.remote_closed());
    HostReadOnPort(host_port, &connection.socket);
  }

  void HostShutdownOnPort(uint32_t host_port, uint32_t flags) {
    RxBuffer* rx_buffer = DoReceive();
    ASSERT_NE(nullptr, rx_buffer);
    VerifyHeader(&rx_buffer->header, host_port, kVirtioVsockGuestPort, 0,
                 VIRTIO_VSOCK_OP_SHUTDOWN, flags);
  }

  void GuestConnectOnPortRequest(uint32_t host_port, uint32_t guest_port) {
    DoSend(host_port, kVirtioVsockGuestCid, guest_port,
           VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_REQUEST);
    RunLoopUntilIdle();
  }

  void GuestConnectInvokeCallbacks(zx_status_t status) {
    for (auto it = connection_requests_.begin();
         it != connection_requests_.end();
         it = connection_requests_.erase(it)) {
      zx::socket socket, remote_socket;
      if (status == ZX_OK) {
        ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &socket,
                                            &remote_socket));
        remote_sockets_.emplace_back(std::move(remote_socket));
      }
      it->callback(status, std::move(socket));
      connections_established_.emplace_back(ConnectionRequest{
          it->src_cid, it->src_port, it->cid, it->port, nullptr});
      RunLoopUntilIdle();
    }
  }

  void GuestConnectOnPortResponse(uint32_t host_port, uint16_t op,
                                  uint32_t guest_port) {
    RxBuffer* rx_buffer = DoReceive();
    ASSERT_NE(nullptr, rx_buffer);
    VerifyHeader(&rx_buffer->header, host_port, guest_port, 0, op, 0);
    if (remote_sockets_.empty()) {
      EXPECT_EQ(rx_buffer->header.buf_alloc, 0u);
    } else {
      EXPECT_GT(rx_buffer->header.buf_alloc, 0u);
    }
    EXPECT_EQ(rx_buffer->header.fwd_cnt, 0u);
  }

  void GuestConnectOnPort(uint32_t host_port, uint32_t guest_port) {
    GuestConnectOnPortRequest(host_port, guest_port);
    GuestConnectInvokeCallbacks(ZX_OK);
    GuestConnectOnPortResponse(host_port, VIRTIO_VSOCK_OP_RESPONSE, guest_port);
  }

  virtio_vsock_hdr_t* GetCreditUpdate() {
    DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
           VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_CREDIT_REQUEST);
    RxBuffer* rx_buffer = DoReceive();
    if (rx_buffer == nullptr) {
      return nullptr;
    }
    VerifyHeader(&rx_buffer->header, kVirtioVsockHostPort,
                 kVirtioVsockGuestPort, 0, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0);
    return &rx_buffer->header;
  }

  void SendCreditUpdate(uint32_t host_port, uint32_t guest_port) {
    DoSend(host_port, kVirtioVsockGuestCid, guest_port,
           VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_CREDIT_UPDATE);
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
  acceptor_->Accept(fuchsia::guest::kHostCid, kVirtioVsockHostPort,
                    kVirtioVsockGuestPort, std::move(connection.remote_socket),
                    connection.callback());
  RunLoopUntilIdle();
  ASSERT_EQ(1u, connection.count);
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, connection.status);
  ASSERT_TRUE(connection.remote_closed());
}

TEST_F(VirtioVsockTest, ConnectRefused) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);

  // Test connection reset.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
         VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_RST);
  RunLoopUntilIdle();

  ASSERT_EQ(1u, connection.count);
  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  ASSERT_TRUE(connection.remote_closed());
  EXPECT_FALSE(vsock_.HasConnection(
      fuchsia::guest::kHostCid, kVirtioVsockHostPort, kVirtioVsockGuestPort));
}

TEST_F(VirtioVsockTest, Listen) {
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort);
  ASSERT_EQ(1u, connections_established_.size());
  ASSERT_TRUE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                   kVirtioVsockHostPort,
                                   kVirtioVsockGuestEphemeralPort));
}

TEST_F(VirtioVsockTest, ListenMultipleTimes) {
  GuestConnectOnPort(kVirtioVsockHostPort + 1,
                     kVirtioVsockGuestEphemeralPort + 1);
  GuestConnectOnPort(kVirtioVsockHostPort + 2,
                     kVirtioVsockGuestEphemeralPort + 2);
  ASSERT_EQ(2u, connections_established_.size());
  ASSERT_TRUE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                   kVirtioVsockHostPort + 1,
                                   kVirtioVsockGuestEphemeralPort + 1));
  ASSERT_TRUE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                   kVirtioVsockHostPort + 2,
                                   kVirtioVsockGuestEphemeralPort + 2));
}

TEST_F(VirtioVsockTest, ListenMultipleTimesSameHostPort) {
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort);
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort + 1);

  EXPECT_TRUE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                   kVirtioVsockHostPort,
                                   kVirtioVsockGuestEphemeralPort));
  EXPECT_TRUE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                   kVirtioVsockHostPort,
                                   kVirtioVsockGuestEphemeralPort + 1));
}

TEST_F(VirtioVsockTest, ListenMultipleTimesSamePort) {
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort);
  EXPECT_TRUE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                   kVirtioVsockHostPort,
                                   kVirtioVsockGuestEphemeralPort));

  GuestConnectOnPortRequest(kVirtioVsockHostPort,
                            kVirtioVsockGuestEphemeralPort);
  GuestConnectOnPortResponse(kVirtioVsockHostPort, VIRTIO_VSOCK_OP_RST,
                             kVirtioVsockGuestEphemeralPort);
  EXPECT_FALSE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                    kVirtioVsockHostPort,
                                    kVirtioVsockGuestEphemeralPort));
}

TEST_F(VirtioVsockTest, ListenRefused) {
  GuestConnectOnPortRequest(kVirtioVsockHostPort,
                            kVirtioVsockGuestEphemeralPort);
  GuestConnectInvokeCallbacks(ZX_ERR_CONNECTION_REFUSED);
  GuestConnectOnPortResponse(kVirtioVsockHostPort, VIRTIO_VSOCK_OP_RST,
                             kVirtioVsockGuestEphemeralPort);
  EXPECT_FALSE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                    kVirtioVsockHostPort,
                                    kVirtioVsockGuestEphemeralPort));
}

TEST_F(VirtioVsockTest, ListenWrongCid) {
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid + 1000,
         kVirtioVsockGuestEphemeralPort, VIRTIO_VSOCK_TYPE_STREAM,
         VIRTIO_VSOCK_OP_REQUEST);
  RunLoopUntilIdle();

  EXPECT_FALSE(vsock_.HasConnection(fuchsia::guest::kHostCid,
                                    kVirtioVsockHostPort,
                                    kVirtioVsockGuestEphemeralPort));
}

TEST_F(VirtioVsockTest, Reset) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  connection.socket.reset();
  RunLoopUntilIdle();
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
}

TEST_F(VirtioVsockTest, ShutdownRead) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(
      connection.socket.write(ZX_SOCKET_SHUTDOWN_WRITE, nullptr, 0, nullptr),
      ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV);
}

TEST_F(VirtioVsockTest, ShutdownWrite) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(
      connection.socket.write(ZX_SOCKET_SHUTDOWN_READ, nullptr, 0, nullptr),
      ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);
}

TEST_F(VirtioVsockTest, WriteAfterShutdown) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(
      connection.socket.write(ZX_SOCKET_SHUTDOWN_READ, nullptr, 0, nullptr),
      ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);

  // Test write after shutdown.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
         VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_RW);

  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(&rx_buffer->header, kVirtioVsockHostPort, kVirtioVsockGuestPort,
               0, VIRTIO_VSOCK_OP_RST, 0);
}

TEST_F(VirtioVsockTest, Read) {
  // Fill a single data buffer in the RxBuffer.
  std::vector<uint8_t> data = {1, 2, 3, 4};
  ASSERT_EQ(data.size(), kDataSize);

  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostReadOnPort(kVirtioVsockHostPort, &connection.socket, data);
  HostReadOnPort(kVirtioVsockHostPort, &connection.socket, data);
}

TEST_F(VirtioVsockTest, ReadChained) {
  // Fill both data buffers in the RxBuffer.
  std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
  ASSERT_EQ(data.size(), 2 * kDataSize);

  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostReadOnPort(kVirtioVsockHostPort, &connection.socket, data);
  HostReadOnPort(kVirtioVsockHostPort, &connection.socket, data);
}

TEST_F(VirtioVsockTest, ReadNoBuffer) {
  // Set the guest buf_alloc to something smaller than our data transfer.
  buf_alloc = 2;
  std::vector<uint8_t> expected = {1, 2, 3, 4};
  ASSERT_EQ(expected.size(), 2 * buf_alloc);

  // Setup connection.
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  // Write data to socket.
  size_t actual;
  ASSERT_EQ(
      connection.socket.write(0, expected.data(), expected.size(), &actual),
      ZX_OK);
  EXPECT_EQ(actual, expected.size());

  // Expect the guest to pull off |buf_alloc| bytes.
  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(&rx_buffer->header, kVirtioVsockHostPort, kVirtioVsockGuestPort,
               buf_alloc, VIRTIO_VSOCK_OP_RW, 0);
  EXPECT_EQ(memcmp(rx_buffer->data, expected.data(), buf_alloc), 0);

  // Update credit to indicate the in-flight bytes have been free'd.
  fwd_cnt += buf_alloc;
  SendCreditUpdate(kVirtioVsockHostPort, kVirtioVsockGuestPort);

  // Expect to receive the remaining bytes
  rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(&rx_buffer->header, kVirtioVsockHostPort, kVirtioVsockGuestPort,
               buf_alloc, VIRTIO_VSOCK_OP_RW, 0);
  EXPECT_EQ(memcmp(rx_buffer->data, expected.data() + buf_alloc, buf_alloc), 0);
}

TEST_F(VirtioVsockTest, Write) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostWriteOnPort(kVirtioVsockHostPort, &connection.socket);
  HostWriteOnPort(kVirtioVsockHostPort, &connection.socket);
}

struct SingleBytePacket {
  virtio_vsock_hdr_t header;
  char c;

  SingleBytePacket(char c_) : c(c_) {}
} __PACKED;

TEST_F(VirtioVsockTest, WriteMultiple) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  SingleBytePacket p1('a');
  SingleBytePacket p2('b');
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p1),
                       sizeof(p1));
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p2),
                       sizeof(p2));
  RunLoopUntilIdle();

  size_t actual_len = 0;
  uint8_t actual_data[3] = {};
  ASSERT_EQ(
      connection.socket.read(0, actual_data, sizeof(actual_data), &actual_len),
      ZX_OK);
  ASSERT_EQ(2u, actual_len);
  ASSERT_EQ('a', actual_data[0]);
  ASSERT_EQ('b', actual_data[1]);
}

TEST_F(VirtioVsockTest, WriteUpdateCredit) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  SingleBytePacket p1('a');
  SingleBytePacket p2('b');
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p1),
                       sizeof(p1));
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p2),
                       sizeof(p2));
  RunLoopUntilIdle();

  // Request credit update, expect 0 fwd_cnt bytes as the data is still in the
  // socket.
  virtio_vsock_hdr_t* header = GetCreditUpdate();
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_CREDIT_UPDATE);
  EXPECT_GT(header->buf_alloc, 0u);
  EXPECT_EQ(header->fwd_cnt, 0u);

  // Read from socket.
  size_t actual_len = 0;
  uint8_t actual_data[3] = {};
  ASSERT_EQ(
      connection.socket.read(0, actual_data, sizeof(actual_data), &actual_len),
      ZX_OK);
  ASSERT_EQ(2u, actual_len);
  ASSERT_EQ('a', actual_data[0]);
  ASSERT_EQ('b', actual_data[1]);

  // Request credit update, expect 2 fwd_cnt bytes as the data has been
  // extracted from the socket.
  header = GetCreditUpdate();
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_CREDIT_UPDATE);
  EXPECT_GT(header->buf_alloc, 0u);
  EXPECT_EQ(header->fwd_cnt, 2u);
}

TEST_F(VirtioVsockTest, WriteSocketFullReset) {
  // If the guest writes enough bytes to overflow our socket buffer then we
  // must reset the connection as we would lose data.
  //
  // 5.7.6.3.1: VIRTIO_VSOCK_OP_RW data packets MUST only be transmitted when
  // the peer has sufficient free buffer space for the payload.
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  size_t socket_size = 0;
  ASSERT_EQ(ZX_OK,
            connection.socket.get_property(ZX_PROP_SOCKET_TX_BUF_MAX,
                                           &socket_size, sizeof(socket_size)));
  size_t buf_size = socket_size + sizeof(virtio_vsock_hdr_t) + 1;
  auto buf = std::make_unique<uint8_t[]>(buf_size);
  memset(buf.get(), 'a', buf_size);

  // Queue one descriptor that will completely fill the socket (and then some),
  // We'll verify that this resets the connection.
  HostQueueWriteOnPort(kVirtioVsockHostPort, buf.get(), buf_size);
  RunLoopUntilIdle();

  RxBuffer* reset = DoReceive();
  ASSERT_NE(nullptr, reset);
  VerifyHeader(&reset->header, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0,
               VIRTIO_VSOCK_OP_RST, 0);
}

TEST_F(VirtioVsockTest, SendCreditUpdateWhenSocketIsDrained) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  // Fill socket buffer completely.
  size_t socket_size = 0;
  ASSERT_EQ(ZX_OK,
            connection.socket.get_property(ZX_PROP_SOCKET_TX_BUF_MAX,
                                           &socket_size, sizeof(socket_size)));
  size_t buf_size = socket_size + sizeof(virtio_vsock_hdr_t);
  auto buf = std::make_unique<uint8_t[]>(buf_size);
  memset(buf.get(), 'a', buf_size);
  HostQueueWriteOnPort(kVirtioVsockHostPort, buf.get(), buf_size);
  RunLoopUntilIdle();

  // No buffers should be available to read.
  ASSERT_EQ(nullptr, DoReceive());

  // Read a single byte from socket to free up space in the socket buffer and
  // make the socket writable again.
  memset(buf.get(), 0, buf_size);
  uint8_t byte;
  size_t actual_len = 0;
  ASSERT_EQ(connection.socket.read(0, &byte, 1, &actual_len), ZX_OK);
  ASSERT_EQ(1u, actual_len);
  ASSERT_EQ('a', byte);

  // Verify we get a credit update now that the socket is wirtable.
  RunLoopUntilIdle();
  RxBuffer* credit_update = DoReceive();
  ASSERT_NE(credit_update, nullptr);
  ASSERT_EQ(socket_size, credit_update->header.buf_alloc);
  ASSERT_EQ(actual_len, credit_update->header.fwd_cnt);
}

TEST_F(VirtioVsockTest, MultipleConnections) {
  TestConnection a_connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort + 1000, &a_connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort + 1000);

  TestConnection b_connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort + 2000, &b_connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort + 2000);

  for (auto i = 0; i < (kVirtioVsockRxBuffers / 4); i++) {
    HostReadOnPort(kVirtioVsockHostPort + 1000, &a_connection.socket);
    HostReadOnPort(kVirtioVsockHostPort + 2000, &b_connection.socket);
    HostWriteOnPort(kVirtioVsockHostPort + 1000, &a_connection.socket);
    HostWriteOnPort(kVirtioVsockHostPort + 2000, &b_connection.socket);
  }
}

TEST_F(VirtioVsockTest, CreditRequest) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  // Test credit request.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
         VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_CREDIT_REQUEST);

  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(&rx_buffer->header, kVirtioVsockHostPort, kVirtioVsockGuestPort,
               0, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0);
  EXPECT_GT(rx_buffer->header.buf_alloc, 0u);
  EXPECT_EQ(rx_buffer->header.fwd_cnt, 0u);
}

TEST_F(VirtioVsockTest, UnsupportedSocketType) {
  // Test connection request with invalid type.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
         UINT16_MAX, VIRTIO_VSOCK_OP_REQUEST);

  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  virtio_vsock_hdr_t* rx_header = &rx_buffer->header;
  EXPECT_EQ(rx_header->src_cid, fuchsia::guest::kHostCid);
  EXPECT_EQ(rx_header->dst_cid, kVirtioVsockGuestCid);
  EXPECT_EQ(rx_header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(rx_header->dst_port, kVirtioVsockGuestPort);
  EXPECT_EQ(rx_header->type, VIRTIO_VSOCK_TYPE_STREAM);
  EXPECT_EQ(rx_header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(rx_header->flags, 0u);
}

}  // namespace
}  // namespace machina
