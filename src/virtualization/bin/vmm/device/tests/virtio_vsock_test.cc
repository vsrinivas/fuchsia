// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <virtio/vsock.h>

#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

namespace {

using ::fuchsia::virtualization::HostVsockAcceptor;
using ::fuchsia::virtualization::HostVsockEndpoint;
using ::fuchsia::virtualization::HostVsockEndpoint_Connect_Result;
using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;
using ::fuchsia::virtualization::HostVsockEndpointPtr;
using ::fuchsia::virtualization::HostVsockEndpointSyncPtr;
using ::fuchsia::virtualization::hardware::VirtioVsock;
using ::fuchsia::virtualization::hardware::VirtioVsock_Start_Result;
using ::fuchsia::virtualization::hardware::VirtioVsockSyncPtr;

constexpr uint32_t kVirtioVsockFirstEphemeralPort = 49152;
constexpr uint32_t kVirtioVsockHostPort = 22;
constexpr uint32_t kVirtioVsockGuestPort = 23;
constexpr uint64_t kGuestCid = fuchsia::virtualization::DEFAULT_GUEST_CID;
constexpr const char* kComponentName = "virtio_vsock";
constexpr const char* kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_vsock#meta/virtio_vsock.cm";

struct RxBuffer {
  // The number of virtio descriptors to use for this buffer (1 descriptor for the header, 3 for
  // data segments).
  static constexpr size_t kNumDescriptors = 4;

  // The number of used bytes, as reported by the device when the descriptor
  // was returned.
  size_t used_bytes;

  virtio_vsock_hdr_t* header;

  // Size for each data descriptor.
  static constexpr size_t kDataSize = 4;
  uint8_t* data1;
  uint8_t* data2;
  uint8_t* data3;

  std::vector<uint8_t> data() const {
    std::vector<uint8_t> result;
    for (uint32_t i = 0; i < used_bytes - sizeof(virtio_vsock_hdr_t); i++) {
      uint8_t* buffer = nullptr;

      switch (i / kDataSize) {
        case 0:
          buffer = data1;
          break;
        case 1:
          buffer = data2;
          break;
        case 2:
          buffer = data3;
          break;
        default:
          assert(false);
      }

      result.push_back(buffer[i % kDataSize]);
    }

    return result;
  }
};

constexpr uint16_t kVirtioRxQueueId = 0;
constexpr uint16_t kVirtioTxQueueId = 1;
constexpr uint16_t kVirtioEventQueueId = 2;
constexpr uint16_t kVirtioVsockRxBuffers = 8;
constexpr uint16_t kVirtioVsockQueueSize = kVirtioVsockRxBuffers * RxBuffer::kNumDescriptors;

enum QueueId {
  RxQueueId = 0,
  TxQueueId = 1,
  EventQueueId = 2,
};

class TestConnection {
 public:
  TestConnection() = default;
  TestConnection(zx::socket socket, uint32_t guest_port, uint32_t host_port)
      : host_port_(host_port), guest_port_(guest_port), socket_(std::move(socket)) {}

  HostVsockEndpoint::ConnectCallback callback() {
    return [this](HostVsockEndpoint_Connect_Result result) {
      callback_count_++;
      if (result.is_response()) {
        status_ = ZX_OK;
        socket_ = std::move(result.response().socket);
      } else {
        status_ = result.err();
      }
    };
  }

  bool SeenNumCallbacks(uint32_t count) const { return callback_count_ == count; }

  void RecordRequestHeaderPorts(const virtio_vsock_hdr_t& header) {
    ASSERT_EQ(header.op, VIRTIO_VSOCK_OP_REQUEST);

    host_port_ = header.src_port;
    guest_port_ = header.dst_port;
  }

  void AssertSocketValid() {
    ASSERT_TRUE(socket_.is_valid());
    ASSERT_EQ(ZX_ERR_TIMED_OUT,
              socket_.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
  }

  void AssertSocketClosed() {
    ASSERT_EQ(ZX_OK, socket_.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
  }

  void AssertSocketReadable() {
    ASSERT_EQ(ZX_OK, socket_.wait_one(ZX_SOCKET_READABLE, zx::time::infinite_past(), nullptr));
  }

  void AssertSocketWritable() {
    ASSERT_EQ(ZX_OK, socket_.wait_one(ZX_SOCKET_WRITABLE, zx::time::infinite_past(), nullptr));
  }

  zx_status_t write(const uint8_t* data, uint32_t size, size_t* actual) {
    return socket_.write(0, data, size, actual);
  }

  zx_status_t read(uint8_t* data, uint32_t size, size_t* actual) {
    return socket_.read(0, data, size, actual);
  }

  uint32_t host_port() const { return host_port_; }
  uint32_t guest_port() const { return guest_port_; }
  zx_status_t status() const { return status_; }
  zx::socket& socket() { return socket_; }

 private:
  uint32_t host_port_;
  uint32_t guest_port_;

  zx::socket socket_;
  uint32_t callback_count_ = 0;
  zx_status_t status_ = ZX_ERR_BAD_STATE;
};

class TestListener : public HostVsockAcceptor {
 public:
  HostVsockEndpoint::ListenCallback ListenCallback() {
    return [this](HostVsockEndpoint_Listen_Result result) {
      status_ = result.is_err() ? result.err() : ZX_OK;
      invoked_listen_callback_ = true;
    };
  }

  bool SeenListenCallback() const { return invoked_listen_callback_; }
  bool ConnectionCountEquals(uint32_t count) const { return requests_.size() == count; }

  // |fuchsia::virtualization::HostVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              HostVsockAcceptor::AcceptCallback callback) override {
    requests_.push_back({src_cid, src_port, port, std::move(callback)});
  }

  void RespondToGuestRequests() {
    for (auto& request : requests_) {
      zx::socket client, remote;
      ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &client, &remote));
      request.callback(fpromise::ok(std::move(remote)));
      connections_.emplace_back(std::move(client), request.src_port, request.port);
    }

    requests_.clear();
    invoked_listen_callback_ = false;
  }

  void RejectGuestRequests() {
    for (auto& request : requests_) {
      request.callback(fpromise::error(ZX_ERR_CONNECTION_REFUSED));
    }

    requests_.clear();
    invoked_listen_callback_ = false;
  }

  zx_status_t status() const { return status_; }

  fidl::InterfaceHandle<HostVsockAcceptor> NewBinding() { return binding_.NewBinding(); }

  struct IncomingRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    HostVsockAcceptor::AcceptCallback callback;
  };

  // Populated by guest initiated requests.
  std::vector<IncomingRequest> requests_;
  std::vector<TestConnection> connections_;

 private:
  bool invoked_listen_callback_ = false;
  zx_status_t status_ = ZX_ERR_BAD_STATE;
  fidl::Binding<HostVsockAcceptor> binding_{this};
};

class VirtioVsockTest : public TestWithDevice {
 protected:
  void SetUp() override {
    auto realm_builder = component_testing::RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, kComponentUrl);

    realm_builder
        .AddRoute(component_testing::Route{
            .capabilities =
                {
                    component_testing::Protocol{fuchsia::logger::LogSink::Name_},
                    component_testing::Protocol{fuchsia::tracing::provider::Registry::Name_},
                },
            .source = component_testing::ParentRef(),
            .targets = {component_testing::ChildRef{kComponentName}}})
        .AddRoute(component_testing::Route{
            .capabilities = {component_testing::Protocol{VirtioVsock::Name_},
                             component_testing::Protocol{HostVsockEndpoint::Name_}},
            .source = component_testing::ChildRef{kComponentName},
            .targets = {component_testing::ParentRef()}});

    realm_ = std::make_unique<component_testing::RealmRoot>(realm_builder.Build(dispatcher()));
    vsock_ = realm_->ConnectSync<VirtioVsock>();
    host_endpoint_ = realm_->Connect<HostVsockEndpoint>();

    rx_queue_ = std::make_unique<VirtioQueueFake>(phys_mem_, PAGE_SIZE, kVirtioVsockQueueSize);
    tx_queue_ = std::make_unique<VirtioQueueFake>(phys_mem_, rx_queue_->end() + PAGE_SIZE * 128,
                                                  kVirtioVsockQueueSize);
    event_queue_ = std::make_unique<VirtioQueueFake>(phys_mem_, tx_queue_->end() + PAGE_SIZE,
                                                     kVirtioVsockQueueSize);

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(event_queue_->end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Start message.
    VirtioVsock_Start_Result result;
    ASSERT_EQ(ZX_OK, vsock_->Start(std::move(start_info), kGuestCid, std::move(initial_listeners_),
                                   &result));
    ASSERT_TRUE(result.is_response());

    // Queue setup.
    rx_queue_->Configure(0, PAGE_SIZE);
    ASSERT_EQ(ZX_OK, vsock_->ConfigureQueue(kVirtioRxQueueId, rx_queue_->size(), rx_queue_->desc(),
                                            rx_queue_->avail(), rx_queue_->used()));
    tx_queue_->Configure(rx_queue_->end(), PAGE_SIZE * 128);
    ASSERT_EQ(ZX_OK, vsock_->ConfigureQueue(kVirtioTxQueueId, tx_queue_->size(), tx_queue_->desc(),
                                            tx_queue_->avail(), tx_queue_->used()));
    event_queue_->Configure(tx_queue_->end(), PAGE_SIZE);
    ASSERT_EQ(ZX_OK, vsock_->ConfigureQueue(kVirtioEventQueueId, event_queue_->size(),
                                            event_queue_->desc(), event_queue_->avail(),
                                            event_queue_->used()));

    // Feature negotiation.
    ASSERT_EQ(ZX_OK, vsock_->Ready(0));

    // Fill RX queue with 8 buffers (32 writable descriptors).
    ASSERT_NO_FATAL_FAILURE(FillRxQueue());
  }

  void HostListenOnPort(uint32_t host_port, TestListener* listener) {
    host_endpoint_->Listen(host_port, listener->NewBinding(), listener->ListenCallback());
    ASSERT_TRUE(
        RunLoopWithTimeoutOrUntil([&] { return listener->SeenListenCallback(); }, zx::sec(5)));
  }

  void HostExpectShutdown(const TestConnection& conn, uint32_t flags) {
    virtio_vsock_hdr_t* header;
    ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_SHUTDOWN));
    ASSERT_EQ(header->dst_port, conn.guest_port());
    ASSERT_EQ(header->src_port, conn.host_port());
    ASSERT_EQ(header->flags, flags);
  }

  void SendToTxQueue(std::vector<std::vector<uint8_t>>& data) {
    DescriptorChainBuilder builder(*tx_queue_);
    for (const auto& it : data) {
      builder.AppendReadableDescriptor(it.data(), static_cast<uint32_t>(it.size()));
    }

    zx_status_t status = builder.Build();
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(ZX_OK, NotifyQueue(TxQueueId));
  }

  void SendPacket(uint32_t host_port, uint32_t guest_port, const std::vector<uint8_t>& bytes) {
    virtio_vsock_hdr_t tx_header = {
        .src_cid = fuchsia::virtualization::DEFAULT_GUEST_CID,
        .dst_cid = fuchsia::virtualization::HOST_CID,
        .src_port = guest_port,
        .dst_port = host_port,
        .len = static_cast<uint32_t>(bytes.size()),
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = VIRTIO_VSOCK_OP_RW,
        .flags = 0,
        .buf_alloc = this->buf_alloc,
        .fwd_cnt = this->fwd_cnt,
    };

    std::vector<std::vector<uint8_t>> buffer;
    buffer.push_back(GetHeaderBytes(tx_header));
    buffer.push_back(bytes);

    ASSERT_NO_FATAL_FAILURE(SendToTxQueue(buffer));

    RunLoopUntilIdle();
  }

  std::vector<uint8_t> GetHeaderBytes(virtio_vsock_hdr_t& header) {
    auto ptr = reinterpret_cast<uint8_t*>(&header);
    auto descriptor = std::vector<uint8_t>(ptr, ptr + sizeof(header));
    return descriptor;
  }

  // Send a packet from the guest to the device.
  void SendHeaderOnlyPacket(uint32_t host_port, uint32_t guest_port, uint16_t op,
                            uint32_t flags = 0,
                            uint64_t dst_cid = fuchsia::virtualization::HOST_CID) {
    virtio_vsock_hdr_t tx_header = {
        .src_cid = fuchsia::virtualization::DEFAULT_GUEST_CID,
        .dst_cid = dst_cid,
        .src_port = guest_port,
        .dst_port = host_port,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = op,
        .flags = flags,
        .buf_alloc = this->buf_alloc,
        .fwd_cnt = this->fwd_cnt,
    };

    std::vector<std::vector<uint8_t>> buffer;
    buffer.push_back(GetHeaderBytes(tx_header));

    ASSERT_NO_FATAL_FAILURE(SendToTxQueue(buffer));

    RunLoopUntilIdle();
  }

  zx_status_t NotifyQueue(QueueId id) { return vsock_->NotifyQueue(id); }

  void FillRxQueue() {
    for (size_t i = 0; i < std::size(rx_buffers_); i++) {
      ASSERT_EQ(ZX_OK,
                DescriptorChainBuilder(*rx_queue_)
                    .AppendWritableDescriptor(&rx_buffers_[i].header, sizeof(virtio_vsock_hdr_t))
                    .AppendWritableDescriptor(&rx_buffers_[i].data1, RxBuffer::kDataSize)
                    .AppendWritableDescriptor(&rx_buffers_[i].data2, RxBuffer::kDataSize)
                    .AppendWritableDescriptor(&rx_buffers_[i].data3, RxBuffer::kDataSize)
                    .Build());
    }

    ASSERT_EQ(ZX_OK, NotifyQueue(RxQueueId));
  }

  void GetNextHeaderOnlyPacketOfType(virtio_vsock_hdr_t** header, uint16_t op) {
    do {
      ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(header));
    } while (*header != nullptr && (*header)->op != op);
  }

  void GetHeaderOnlyPacketFromRxQueue(virtio_vsock_hdr_t** header) {
    *header = nullptr;
    RxBuffer* buffer;
    ASSERT_NO_FATAL_FAILURE(DoReceive(&buffer));
    ASSERT_EQ(buffer->used_bytes, sizeof(virtio_vsock_hdr_t));
    ASSERT_EQ(buffer->header->len, 0u);
    *header = buffer->header;
  }

  void DoReceive(RxBuffer** buffer) {
    *buffer = nullptr;

    auto used = rx_queue_->NextUsed();
    while (!used) {
      ASSERT_EQ(ZX_OK, WaitOnInterrupt());
      used = rx_queue_->NextUsed();
    }

    *buffer = &rx_buffers_[used->id / RxBuffer::kNumDescriptors];
    (*buffer)->used_bytes = used->len;
  }

  void ClientConnectOnPort(uint32_t port, TestConnection& connection) {
    host_endpoint_->Connect(port, connection.callback());

    virtio_vsock_hdr_t* header;
    ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_REQUEST));
    ASSERT_NO_FATAL_FAILURE(connection.RecordRequestHeaderPorts(*header));

    SendHeaderOnlyPacket(header->src_port, header->dst_port, VIRTIO_VSOCK_OP_RESPONSE);
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/1); },
                                          zx::sec(5)));

    // Fetch and discard the initial credit update the device always sends.
    ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_CREDIT_UPDATE));
  }

  void ClientWriteGuestRead(const std::vector<uint8_t>& data, TestConnection& conn) {
    size_t actual;
    ASSERT_EQ(ZX_OK, conn.write(data.data(), static_cast<uint32_t>(data.size()), &actual));
    ASSERT_EQ(data.size(), actual);

    RxBuffer* buffer;
    ASSERT_NO_FATAL_FAILURE(DoReceive(&buffer));
    ASSERT_EQ(buffer->header->op, VIRTIO_VSOCK_OP_RW);

    ASSERT_EQ(buffer->header->src_port, conn.host_port());
    ASSERT_EQ(buffer->header->dst_port, conn.guest_port());
    ASSERT_EQ(buffer->header->len, data.size());
    ASSERT_EQ(buffer->header->len, buffer->used_bytes - sizeof(virtio_vsock_hdr_t));

    ASSERT_EQ(buffer->data(), data);
  }

  void GuestWriteClientRead(const std::vector<uint8_t>& data, TestConnection& conn) {
    SendPacket(conn.host_port(), conn.guest_port(), data);
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&] {
          zx_info_socket_t info;
          if (conn.socket().get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr) !=
              ZX_OK) {
            return false;
          }

          return info.rx_buf_available == data.size();
        },
        zx::sec(5)));

    size_t actual;
    std::vector<uint8_t> actual_data(data.size(), 0);
    ASSERT_EQ(ZX_OK,
              conn.read(actual_data.data(), static_cast<uint32_t>(actual_data.size()), &actual));
    EXPECT_EQ(actual, actual_data.size());
    EXPECT_EQ(data, actual_data);
  }

  VirtioVsockSyncPtr vsock_;
  HostVsockEndpointPtr host_endpoint_;

  std::unique_ptr<component_testing::RealmRoot> realm_;
  std::vector<::fuchsia::virtualization::Listener> initial_listeners_;

  std::unique_ptr<VirtioQueueFake> rx_queue_;
  std::unique_ptr<VirtioQueueFake> tx_queue_;
  std::unique_ptr<VirtioQueueFake> event_queue_;

  // Set some default credit parameters that should suffice for most tests.
  // Tests of the credit system will want to assign a more reasonable buf_alloc
  // value.
  uint32_t buf_alloc = UINT32_MAX;
  uint32_t fwd_cnt = 0;

  RxBuffer rx_buffers_[kVirtioVsockRxBuffers] = {};
};

TEST_F(VirtioVsockTest, ClientInitiatedConnect) {
  TestConnection connection;
  host_endpoint_->Connect(kVirtioVsockGuestPort, connection.callback());

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/1); },
                                        zx::sec(5)));

  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketValid());
}

TEST_F(VirtioVsockTest, ClientConnectMultipleTimesSequentially) {
  TestConnection connection;

  host_endpoint_->Connect(kVirtioVsockGuestPort, connection.callback());

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/1); },
                                        zx::sec(5)));
  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketValid());

  // Guest initiated shutdown.
  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_SHUTDOWN, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);

  // A reset packet in response to a shutdown packet is a clean shutdown.
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RST));
  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketClosed());

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  host_endpoint_->Connect(kVirtioVsockGuestPort, connection.callback());
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_REQUEST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort + 1);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort + 1, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/2); },
                                        zx::sec(5)));

  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketValid());
}

TEST_F(VirtioVsockTest, ClientConnectMultipleTimesParallel) {
  TestConnection connection1, connection2;

  host_endpoint_->Connect(kVirtioVsockGuestPort, connection1.callback());
  host_endpoint_->Connect(kVirtioVsockGuestPort, connection2.callback());

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_REQUEST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_REQUEST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort + 1);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);
  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort + 1, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return connection1.SeenNumCallbacks(/*count=*/1); },
                                        zx::sec(5)));
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return connection2.SeenNumCallbacks(/*count=*/1); },
                                        zx::sec(5)));

  ASSERT_NO_FATAL_FAILURE(connection1.AssertSocketValid());
  ASSERT_NO_FATAL_FAILURE(connection2.AssertSocketValid());
}

TEST_F(VirtioVsockTest, ClientConnectionRefused) {
  TestConnection connection;

  host_endpoint_->Connect(kVirtioVsockGuestPort, connection.callback());

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_REQUEST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  // Guest rejected connection.
  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_RST);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/1); },
                                        zx::sec(5)));
  ASSERT_EQ(connection.status(), ZX_ERR_CONNECTION_REFUSED);
}

TEST_F(VirtioVsockTest, Listen) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener.requests_.size(), 1ul);
  listener.RespondToGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

TEST_F(VirtioVsockTest, NoListener) {
  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  // No listener, so the device sends the guest a RESET packet.
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

TEST_F(VirtioVsockTest, ListenMultipleTimesDifferentHostPorts) {
  TestListener listener1, listener2;
  HostListenOnPort(kVirtioVsockHostPort, &listener1);
  HostListenOnPort(kVirtioVsockHostPort + 1, &listener2);

  ASSERT_EQ(ZX_OK, listener1.status());
  ASSERT_EQ(ZX_OK, listener2.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);
  SendHeaderOnlyPacket(kVirtioVsockHostPort + 1, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener1.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener1.requests_.size(), 1ul);
  listener1.RespondToGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RESPONSE));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener2.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener2.requests_.size(), 1ul);
  listener2.RespondToGuestRequests();

  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RESPONSE));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort + 1);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

TEST_F(VirtioVsockTest, FailWhenListeningOnSameHostPort) {
  {
    TestListener listener1, listener2;
    HostListenOnPort(kVirtioVsockHostPort, &listener1);
    HostListenOnPort(kVirtioVsockHostPort, &listener2);

    ASSERT_EQ(ZX_OK, listener1.status());
    ASSERT_EQ(ZX_ERR_ALREADY_BOUND, listener2.status());
  }

  RunLoopUntilIdle();

  // The acceptor for listener1 has gone out of scope, allowing another listener to bind
  // to the same port.
  TestListener listener3;
  HostListenOnPort(kVirtioVsockHostPort, &listener3);
  ASSERT_EQ(ZX_OK, listener3.status());
}

TEST_F(VirtioVsockTest, GuestInitiatedTwoIdenticalConnections) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener.requests_.size(), 1ul);
  listener.RespondToGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  // Same host/guest port pair.
  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

TEST_F(VirtioVsockTest, GuestInitiatedReuseSamePortAfterCleanShutdown) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener.requests_.size(), 1ul);
  listener.RespondToGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_SHUTDOWN,
                       VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);

  // Clean shutdown, ports can immediately be reused.
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);
  RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(2); }, zx::sec(5));

  ASSERT_EQ(listener.requests_.size(), 1ul);
  listener.RespondToGuestRequests();

  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RESPONSE));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

TEST_F(VirtioVsockTest, GuestInitiatedMultiplexOverOneHostPort) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);
  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort + 1, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(2); }, zx::sec(5)));
  ASSERT_EQ(listener.requests_.size(), 2ul);
  listener.RespondToGuestRequests();

  virtio_vsock_hdr_t *header1, *header2;
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header1, VIRTIO_VSOCK_OP_RESPONSE));
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header2, VIRTIO_VSOCK_OP_RESPONSE));

  // Only packets within one connection are ordered, so responses can come in any order.
  if (header1->dst_port == kVirtioVsockGuestPort) {
    ASSERT_EQ(header2->dst_port, kVirtioVsockGuestPort + 1);
  } else {
    ASSERT_EQ(header1->dst_port, kVirtioVsockGuestPort + 1);
    ASSERT_EQ(header2->dst_port, kVirtioVsockGuestPort);
  }
}

TEST_F(VirtioVsockTest, GuestInitiatedConnectionRefused) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener.requests_.size(), 1ul);
  listener.RejectGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

TEST_F(VirtioVsockTest, GuestInitiatedConnectionWrongCid) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  // The only supported destination CID is the host CID as this doesn't support VM to VM
  // communication.
  constexpr uint64_t kUnexpectedDestinationCid = 12345;

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST,
                       /*flags=*/0, /*dst_cid=*/kUnexpectedDestinationCid);

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  // No listener, so the device sends the guest a RESET packet.
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_cid, kUnexpectedDestinationCid);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

TEST_F(VirtioVsockTest, Reset) {
  TestConnection connection;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, connection));

  connection.socket().reset();

  HostExpectShutdown(connection, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
}

TEST_F(VirtioVsockTest, GuestShutdownRead) {
  TestConnection connection;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, connection));

  SendHeaderOnlyPacket(connection.host_port(), connection.guest_port(), VIRTIO_VSOCK_OP_SHUTDOWN,
                       /*flags=*/VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV);

  // Socket is half closed.
  ASSERT_EQ(ZX_OK, connection.socket().wait_one(ZX_SOCKET_WRITE_DISABLED,
                                                zx::deadline_after(zx::sec(5)), nullptr));

  SendHeaderOnlyPacket(connection.host_port(), connection.guest_port(), VIRTIO_VSOCK_OP_SHUTDOWN,
                       /*flags=*/VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);

  // Socket is fully closed.
  ASSERT_EQ(ZX_OK, connection.socket().wait_one(ZX_SOCKET_PEER_CLOSED,
                                                zx::deadline_after(zx::sec(5)), nullptr));
}

TEST_F(VirtioVsockTest, ShutdownWrite) {
  TestConnection connection;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, connection));

  ASSERT_EQ(connection.socket().set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_DISABLED), ZX_OK);

  std::vector<uint8_t> bytes = {1, 2, 3};
  SendPacket(connection.host_port(), connection.guest_port(), bytes);

  HostExpectShutdown(connection, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);
}

TEST_F(VirtioVsockTest, WriteAfterShutdown) {
  TestConnection connection;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, connection));

  SendHeaderOnlyPacket(connection.host_port(), connection.guest_port(), VIRTIO_VSOCK_OP_SHUTDOWN,
                       /*flags=*/VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);

  // Test write after shutdown.
  std::vector<uint8_t> bytes = {1, 2, 3};
  SendPacket(connection.host_port(), connection.guest_port(), bytes);

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RST));
  ASSERT_EQ(header->dst_port, connection.guest_port());
  ASSERT_EQ(header->src_port, connection.host_port());
}

TEST_F(VirtioVsockTest, Read) {
  // Fill a single data buffer in the RxBuffer.
  std::vector<uint8_t> data = {1, 2, 3, 4};
  ASSERT_EQ(data.size(), RxBuffer::kDataSize);

  TestConnection connection;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, connection));
  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketValid());

  ASSERT_NO_FATAL_FAILURE(ClientWriteGuestRead(data, connection));
  ASSERT_NO_FATAL_FAILURE(ClientWriteGuestRead(data, connection));
}

TEST_F(VirtioVsockTest, ReadChained) {
  // Fill both data buffers in the RxBuffer.
  std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
  ASSERT_EQ(data.size(), 2 * RxBuffer::kDataSize);

  TestConnection connection;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, connection));
  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketValid());

  ASSERT_NO_FATAL_FAILURE(ClientWriteGuestRead(data, connection));
  ASSERT_NO_FATAL_FAILURE(ClientWriteGuestRead(data, connection));
}

TEST_F(VirtioVsockTest, ReadNoBuffer) {
  // Set the guest buf_alloc to something smaller than our data transfer.
  buf_alloc = 2;
  std::vector<uint8_t> expected = {1, 2, 3, 4};
  ASSERT_EQ(expected.size(), 2 * buf_alloc);

  // Setup connection.
  TestConnection conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, conn));
  ASSERT_NO_FATAL_FAILURE(conn.AssertSocketValid());

  // Write data to socket.
  size_t actual;
  ASSERT_EQ(ZX_OK, conn.write(expected.data(), static_cast<uint32_t>(expected.size()), &actual));
  ASSERT_EQ(expected.size(), actual);

  // Expect the guest to pull off |buf_alloc| bytes.
  RxBuffer* buffer;
  ASSERT_NO_FATAL_FAILURE(DoReceive(&buffer));
  ASSERT_EQ(buffer->header->op, VIRTIO_VSOCK_OP_RW);
  ASSERT_EQ(buffer->header->len, buf_alloc);

  ASSERT_EQ(buffer->data(), std::vector<uint8_t>(expected.begin(), expected.begin() + buf_alloc));

  // Update credit to indicate the in-flight bytes have been freed.
  fwd_cnt += buf_alloc;

  SendHeaderOnlyPacket(conn.host_port(), conn.guest_port(), VIRTIO_VSOCK_OP_CREDIT_UPDATE);

  // Expect to receive the remaining bytes.
  ASSERT_NO_FATAL_FAILURE(DoReceive(&buffer));
  ASSERT_EQ(buffer->header->op, VIRTIO_VSOCK_OP_RW);
  ASSERT_EQ(buffer->header->len, buf_alloc);

  ASSERT_EQ(buffer->data(), std::vector<uint8_t>(expected.begin() + buf_alloc, expected.end()));
}

TEST_F(VirtioVsockTest, Write) {
  TestConnection conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, conn));
  ASSERT_NO_FATAL_FAILURE(conn.AssertSocketValid());

  std::vector<uint8_t> data = {1, 2, 3, 4};
  ASSERT_NO_FATAL_FAILURE(GuestWriteClientRead(data, conn));
  ASSERT_NO_FATAL_FAILURE(GuestWriteClientRead(data, conn));
}

TEST_F(VirtioVsockTest, ClientWriteWithInitialCredit) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener.requests_.size(), 1ul);
  listener.RespondToGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_CREDIT_UPDATE));

  // The guest sends its initial credit information with the VIRTIO_VSOCK_OP_REQUEST packet, so
  // a client should immediately have credit to write to the guest with.
  std::vector<uint8_t> data = {1, 2, 3, 4};
  ASSERT_EQ(data.size(), RxBuffer::kDataSize);

  ASSERT_EQ(listener.connections_.size(), 1ul);
  ASSERT_NO_FATAL_FAILURE(ClientWriteGuestRead(data, listener.connections_[0]));
}

TEST_F(VirtioVsockTest, WriteMultiple) {
  TestConnection conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, conn));
  ASSERT_NO_FATAL_FAILURE(conn.AssertSocketValid());

  SendPacket(conn.host_port(), conn.guest_port(), {'a'});
  SendPacket(conn.host_port(), conn.guest_port(), {'b'});

  // Wait for the two bytes to appear on the client socket (or a 5s timeout).
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] {
        zx_info_socket_t info;
        if (conn.socket().get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr) !=
            ZX_OK) {
          return false;
        }

        return info.rx_buf_available == 2;
      },
      zx::sec(5)));

  size_t actual;
  std::vector<uint8_t> actual_data(2, 0);
  ASSERT_EQ(ZX_OK,
            conn.read(actual_data.data(), static_cast<uint32_t>(actual_data.size()), &actual));
  ASSERT_EQ(actual, actual_data.size());

  EXPECT_EQ('a', actual_data[0]);
  EXPECT_EQ('b', actual_data[1]);
}

TEST_F(VirtioVsockTest, WriteUpdateCredit) {
  TestConnection conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, conn));
  ASSERT_NO_FATAL_FAILURE(conn.AssertSocketValid());

  SendPacket(conn.host_port(), conn.guest_port(), {'a'});
  SendPacket(conn.host_port(), conn.guest_port(), {'b'});

  // Request credit update, expect 0 fwd_cnt bytes as the data is still in the
  // socket.
  SendHeaderOnlyPacket(conn.host_port(), conn.guest_port(), VIRTIO_VSOCK_OP_CREDIT_REQUEST);

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_CREDIT_UPDATE);
  EXPECT_GT(header->buf_alloc, 0u);
  EXPECT_EQ(header->fwd_cnt, 0u);

  // Read from socket.
  size_t actual;
  std::vector<uint8_t> actual_data(2, 0);
  ASSERT_EQ(ZX_OK,
            conn.read(actual_data.data(), static_cast<uint32_t>(actual_data.size()), &actual));
  ASSERT_EQ(actual, actual_data.size());

  EXPECT_EQ('a', actual_data[0]);
  EXPECT_EQ('b', actual_data[1]);

  // Request credit update, expect 2 fwd_cnt bytes as the data has been
  // extracted from the socket.
  SendHeaderOnlyPacket(conn.host_port(), conn.guest_port(), VIRTIO_VSOCK_OP_CREDIT_REQUEST);

  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_CREDIT_UPDATE);
  EXPECT_GT(header->buf_alloc, 0u);
  EXPECT_EQ(header->fwd_cnt, 2u);
}

TEST_F(VirtioVsockTest, WriteMultipleConnections) {
  TestConnection a_conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, a_conn));
  ASSERT_NO_FATAL_FAILURE(a_conn.AssertSocketValid());

  TestConnection b_conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, b_conn));
  ASSERT_NO_FATAL_FAILURE(b_conn.AssertSocketValid());

  std::vector<uint8_t> data1 = {1, 2, 3, 4};
  std::vector<uint8_t> data2 = {5, 6, 7, 8};

  ASSERT_NO_FATAL_FAILURE(GuestWriteClientRead(data1, a_conn));
  ASSERT_NO_FATAL_FAILURE(GuestWriteClientRead(data2, b_conn));
  ASSERT_NO_FATAL_FAILURE(ClientWriteGuestRead(data1, a_conn));
  ASSERT_NO_FATAL_FAILURE(ClientWriteGuestRead(data2, b_conn));
}

TEST_F(VirtioVsockTest, WriteSocketFullReset) {
  // If the guest writes enough bytes to overflow our socket buffer then we
  // must reset the connection as we would lose data.
  //
  // 5.7.6.3.1: VIRTIO_VSOCK_OP_RW data packets MUST only be transmitted when
  // the peer has sufficient free buffer space for the payload.
  TestConnection conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, conn));
  ASSERT_NO_FATAL_FAILURE(conn.AssertSocketValid());

  SendHeaderOnlyPacket(conn.host_port(), conn.guest_port(), VIRTIO_VSOCK_OP_CREDIT_REQUEST);

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_CREDIT_UPDATE);

  // This is one byte more than the reported credit, which should reset the connection.
  std::vector<uint8_t> buffer(header->buf_alloc + 1, 'a');
  SendPacket(conn.host_port(), conn.guest_port(), buffer);

  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_port, conn.host_port());
  EXPECT_EQ(header->dst_port, conn.guest_port());
}

TEST_F(VirtioVsockTest, SendCreditUpdateWhenSocketIsDrained) {
  TestConnection conn;
  ASSERT_NO_FATAL_FAILURE(ClientConnectOnPort(kVirtioVsockGuestPort, conn));
  ASSERT_NO_FATAL_FAILURE(conn.AssertSocketValid());

  SendHeaderOnlyPacket(conn.host_port(), conn.guest_port(), VIRTIO_VSOCK_OP_CREDIT_REQUEST);

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_CREDIT_UPDATE);

  // Fill socket buffer completely.
  std::vector<uint8_t> buffer(header->buf_alloc, 'a');
  SendPacket(conn.host_port(), conn.guest_port(), buffer);

  // Wait for the out of process device to process the packet and write it to the socket.
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] {
        zx_info_socket_t info;
        if (conn.socket().get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr) !=
            ZX_OK) {
          return false;
        }

        return info.rx_buf_available == buffer.size();
      },
      zx::sec(5)));

  // Read a single byte from socket to free up space in the socket buffer and
  // make the socket writable again.
  uint8_t byte;
  size_t actual_len = 0;
  ASSERT_EQ(ZX_OK, conn.read(&byte, 1, &actual_len));
  ASSERT_EQ(1u, actual_len);
  ASSERT_EQ('a', byte);

  // Verify we get a credit update now that the socket is writable.
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_CREDIT_UPDATE);
  ASSERT_EQ(header->fwd_cnt, actual_len);
}

TEST_F(VirtioVsockTest, NoResponseToSpuriousReset) {
  // Spurious reset for a non-existent connection.
  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_RST);

  TestConnection connection;
  host_endpoint_->Connect(kVirtioVsockGuestPort, connection.callback());

  // The spurious reset packet didn't result in the device sending a reset back to the guest, so
  // after creating a connection the first packet on the RX queue is a connection request.
  RxBuffer* buffer;
  ASSERT_NO_FATAL_FAILURE(DoReceive(&buffer));
  ASSERT_EQ(buffer->header->op, VIRTIO_VSOCK_OP_REQUEST);
}

TEST_F(VirtioVsockTest, NonResetSpuriousPacketsGetResetResponse) {
  for (uint16_t packet_op : std::vector<uint16_t>{
           VIRTIO_VSOCK_OP_SHUTDOWN,
           VIRTIO_VSOCK_OP_RESPONSE,
           VIRTIO_VSOCK_OP_CREDIT_UPDATE,
           VIRTIO_VSOCK_OP_CREDIT_REQUEST,
           VIRTIO_VSOCK_OP_INVALID,
           VIRTIO_VSOCK_OP_RW,
       }) {
    SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, packet_op);

    virtio_vsock_hdr_t* header;
    ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

    EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
    EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
    EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
  }
}

TEST_F(VirtioVsockTest, UnsupportedSocketType) {
  // Only VIRTIO_VSOCK_TYPE_STREAM is currently supported.
  uint16_t VIRTIO_VSOCK_TYPE_SEQPACKET = 2;

  virtio_vsock_hdr_t tx_header = {
      .src_cid = fuchsia::virtualization::DEFAULT_GUEST_CID,
      .dst_cid = fuchsia::virtualization::HOST_CID,
      .src_port = kVirtioVsockGuestPort,
      .dst_port = kVirtioVsockHostPort,
      .type = VIRTIO_VSOCK_TYPE_SEQPACKET,
      .op = VIRTIO_VSOCK_OP_REQUEST,
      .flags = 0,
      .buf_alloc = this->buf_alloc,
      .fwd_cnt = this->fwd_cnt,
  };

  std::vector<std::vector<uint8_t>> buffer;
  buffer.push_back(GetHeaderBytes(tx_header));

  ASSERT_NO_FATAL_FAILURE(SendToTxQueue(buffer));

  RunLoopUntilIdle();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
}

TEST_F(VirtioVsockTest, SkipBadRxDescriptors) {
  // First empty the RX queue of available chains.
  for (size_t i = 0; i < std::size(rx_buffers_); i++) {
    SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_SHUTDOWN);

    virtio_vsock_hdr_t* header;
    ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));
    EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  }

  // Too small.
  ASSERT_EQ(ZX_OK,
            DescriptorChainBuilder(*rx_queue_)
                .AppendWritableDescriptor(&rx_buffers_[0].header, sizeof(virtio_vsock_hdr_t) / 2)
                .Build());

  // Contains wrong type.
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(*rx_queue_)
                       .AppendReadableDescriptor(&rx_buffers_[0].header, sizeof(virtio_vsock_hdr_t))
                       .AppendWritableDescriptor(&rx_buffers_[0].header, sizeof(virtio_vsock_hdr_t))
                       .Build());

  // Add valid chains back into the RX queue.
  ASSERT_NO_FATAL_FAILURE(FillRxQueue());

  // Ignore the two bad chains which both went unusued.
  RxBuffer* buffer;
  ASSERT_NO_FATAL_FAILURE(DoReceive(&buffer));
  ASSERT_EQ(buffer->used_bytes, 0ul);

  ASSERT_NO_FATAL_FAILURE(DoReceive(&buffer));
  ASSERT_EQ(buffer->used_bytes, 0ul);

  // Get another reset packet using one of the valid chains.
  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_SHUTDOWN);
  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));
  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
}

class VirtioVsockInitialListenerTest : public VirtioVsockTest {};

TEST_F(VirtioVsockInitialListenerTest, GuestConnectToInitialListener) {
  TestListener listener1, listener2, listener3;

  initial_listeners_.push_back({123, listener1.NewBinding()});
  initial_listeners_.push_back({kVirtioVsockHostPort, listener2.NewBinding()});
  initial_listeners_.push_back({789, listener3.NewBinding()});

  ASSERT_NO_FATAL_FAILURE(VirtioVsockTest::SetUp());

  // Guest initiated request to a listener passed via the start message.
  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return listener2.ConnectionCountEquals(1); }, zx::sec(5)));
  ASSERT_EQ(listener2.requests_.size(), 1ul);
  listener2.RespondToGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

}  // namespace
