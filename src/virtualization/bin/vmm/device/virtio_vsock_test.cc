// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <src/virtualization/bin/vmm/device/test_with_device.h>
#include <src/virtualization/bin/vmm/device/virtio_queue_fake.h>
#include <virtio/vsock.h>

// These tests have been ported over from the in-process virtio-vsock device test suite, but the
// out-of-process virtio-vsock device is does not yet support all functionality. These tests
// have been left in this file to allow incrementally re-enabling them as functionality is added
// to the device.
//
// Note that many of these tests don't compile in their current state as boilerplate and helper
// functions need to be changed.
//
// See fxb/97355 for more information.
#define ENABLE_UNSUPPORTED_TESTS 0

namespace {

using ::fuchsia::virtualization::HostVsockAcceptor;
using ::fuchsia::virtualization::HostVsockEndpoint;
using ::fuchsia::virtualization::HostVsockEndpoint_Connect2_Result;
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
  HostVsockEndpoint::Connect2Callback callback() {
    return [this](HostVsockEndpoint_Connect2_Result result) {
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

  zx::socket& socket() { return socket_; }

  void AssertSocketClosed() {
    ASSERT_EQ(ZX_OK, socket_.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
  }

  void AssertSocketValid() {
    ASSERT_TRUE(socket_.is_valid());
    ASSERT_EQ(ZX_ERR_TIMED_OUT,
              socket_.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), nullptr));
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

  zx_status_t status() const { return status_; }

 private:
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
      client_sockets_.push_back(std::move(client));
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
  std::vector<zx::socket> client_sockets_;

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
    tx_queue_ = std::make_unique<VirtioQueueFake>(phys_mem_, rx_queue_->end() + PAGE_SIZE,
                                                  kVirtioVsockQueueSize);
    event_queue_ = std::make_unique<VirtioQueueFake>(phys_mem_, tx_queue_->end() + PAGE_SIZE,
                                                     kVirtioVsockQueueSize);

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(event_queue_->end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Start message.
    VirtioVsock_Start_Result result;
    ASSERT_EQ(ZX_OK, vsock_->Start(std::move(start_info), kGuestCid, &result));
    ASSERT_TRUE(result.is_response());

    // Queue setup.
    rx_queue_->Configure(0, PAGE_SIZE);
    ASSERT_EQ(ZX_OK, vsock_->ConfigureQueue(kVirtioRxQueueId, rx_queue_->size(), rx_queue_->desc(),
                                            rx_queue_->avail(), rx_queue_->used()));
    tx_queue_->Configure(rx_queue_->end(), PAGE_SIZE);
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
    RunLoopWithTimeoutOrUntil([&] { return listener->SeenListenCallback(); }, zx::sec(5));
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

  VirtioVsockSyncPtr vsock_;
  HostVsockEndpointPtr host_endpoint_;

  std::unique_ptr<component_testing::RealmRoot> realm_;

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
  host_endpoint_->Connect2(kVirtioVsockGuestPort, connection.callback());

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);
  RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/1); }, zx::sec(5));

  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketValid());
}

TEST_F(VirtioVsockTest, ClientConnectMultipleTimesSequentially) {
  TestConnection connection;

  host_endpoint_->Connect2(kVirtioVsockGuestPort, connection.callback());

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);
  RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/1); }, zx::sec(5));
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

  host_endpoint_->Connect2(kVirtioVsockGuestPort, connection.callback());
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_REQUEST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort + 1);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort + 1, kVirtioVsockGuestPort,
                       VIRTIO_VSOCK_OP_RESPONSE);
  RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/2); }, zx::sec(5));

  ASSERT_NO_FATAL_FAILURE(connection.AssertSocketValid());
}

TEST_F(VirtioVsockTest, ClientConnectMultipleTimesParallel) {
  TestConnection connection1, connection2;

  host_endpoint_->Connect2(kVirtioVsockGuestPort, connection1.callback());
  host_endpoint_->Connect2(kVirtioVsockGuestPort, connection2.callback());

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

  RunLoopWithTimeoutOrUntil([&] { return connection1.SeenNumCallbacks(/*count=*/1); }, zx::sec(5));
  RunLoopWithTimeoutOrUntil([&] { return connection2.SeenNumCallbacks(/*count=*/1); }, zx::sec(5));

  ASSERT_NO_FATAL_FAILURE(connection1.AssertSocketValid());
  ASSERT_NO_FATAL_FAILURE(connection2.AssertSocketValid());
}

TEST_F(VirtioVsockTest, ClientConnectionRefused) {
  TestConnection connection;

  host_endpoint_->Connect2(kVirtioVsockGuestPort, connection.callback());

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_REQUEST));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_REQUEST);
  EXPECT_EQ(header->src_port, kVirtioVsockFirstEphemeralPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  // Guest rejected connection.
  SendHeaderOnlyPacket(kVirtioVsockFirstEphemeralPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_RST);

  RunLoopWithTimeoutOrUntil([&] { return connection.SeenNumCallbacks(/*count=*/1); }, zx::sec(5));
  ASSERT_EQ(connection.status(), ZX_ERR_CONNECTION_REFUSED);
}

TEST_F(VirtioVsockTest, Listen) {
  TestListener listener;
  HostListenOnPort(kVirtioVsockHostPort, &listener);
  ASSERT_EQ(ZX_OK, listener.status());

  SendHeaderOnlyPacket(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_OP_REQUEST);

  RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5));
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

  RunLoopWithTimeoutOrUntil([&] { return listener1.ConnectionCountEquals(1); }, zx::sec(5));
  ASSERT_EQ(listener1.requests_.size(), 1ul);
  listener1.RespondToGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetNextHeaderOnlyPacketOfType(&header, VIRTIO_VSOCK_OP_RESPONSE));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RESPONSE);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);

  RunLoopWithTimeoutOrUntil([&] { return listener2.ConnectionCountEquals(1); }, zx::sec(5));
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

  RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5));
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

  RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5));
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

  RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(2); }, zx::sec(5));
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

  RunLoopWithTimeoutOrUntil([&] { return listener.ConnectionCountEquals(1); }, zx::sec(5));
  ASSERT_EQ(listener.requests_.size(), 1ul);
  listener.RejectGuestRequests();

  virtio_vsock_hdr_t* header;
  ASSERT_NO_FATAL_FAILURE(GetHeaderOnlyPacketFromRxQueue(&header));

  EXPECT_EQ(header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(header->dst_port, kVirtioVsockGuestPort);
}

#if ENABLE_UNSUPPORTED_TESTS

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

TEST_F(VirtioVsockTest, ConnectEarlyData) {
  TestConnection connection;

  // Put some initial data on the connection
  size_t actual;
  ASSERT_EQ(
      connection.write(kDefaultData.data(), static_cast<uint32_t>(kDefaultData.size()), &actual),
      ZX_OK);

  HostConnectOnPort(kVirtioVsockHostPort, &connection);
}

TEST_F(VirtioVsockTest, Reset) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  connection.socket().reset();
  RunLoopUntilIdle();
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
}

// The device should not send any response to a spurious reset packet.
TEST_F(VirtioVsockTest, NoResponseToSpuriousReset) {
  // Send a reset from the guest.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
         VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_RST);
  RunLoopUntilIdle();

  // Expect no response from the host.
  RxBuffer* buffer = DoReceive();
  EXPECT_EQ(nullptr, buffer);
}

TEST_F(VirtioVsockTest, ShutdownRead) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(connection.socket().set_disposition(ZX_SOCKET_DISPOSITION_WRITE_DISABLED, 0), ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV);
}

TEST_F(VirtioVsockTest, ShutdownWrite) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(connection.socket().set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_DISABLED), ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);
}

// Ensure endpoints are notified when a socket has been shutdown.
TEST_F(VirtioVsockTest, ShutdownNotifiesEndpoint) {
  TestConnection connection;

  // Establish a connection between the host and guest.
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  // Have the guest shutdown the stream.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
         VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_SHUTDOWN,
         /*flags=*/VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
  RunLoopUntilIdle();

  // Ensure the host reset the connection.
  ExpectHostResetOnPort(kVirtioVsockHostPort);
  RunLoopUntilIdle();

  // Ensure the endpoint was sent a shutdown event.
  ASSERT_EQ(received_shutdown_events().size(), 1u);
  const ShutdownEvent& shutdown_event = received_shutdown_events()[0];
  EXPECT_EQ(shutdown_event.guest_cid, kVirtioVsockGuestCid);
  EXPECT_EQ(shutdown_event.local_cid, fuchsia::virtualization::HOST_CID);
  EXPECT_EQ(shutdown_event.local_port, kVirtioVsockHostPort);
  EXPECT_EQ(shutdown_event.remote_port, kVirtioVsockGuestPort);
}

// Endpoints should not be sent OnShutdown events when virtio-vsock is
// merely responding to spurious packets.
TEST_F(VirtioVsockTest, SpuriousPacketsDoNotNotifyEndpoints) {
  for (uint16_t packet_op : std::vector<uint16_t>{
           VIRTIO_VSOCK_OP_SHUTDOWN,
           VIRTIO_VSOCK_OP_RESPONSE,
           VIRTIO_VSOCK_OP_CREDIT_UPDATE,
           VIRTIO_VSOCK_OP_CREDIT_REQUEST,
           VIRTIO_VSOCK_OP_INVALID,
           VIRTIO_VSOCK_OP_RW,
       }) {
    SCOPED_TRACE(testing::Message() << "Testing packet operation #" << packet_op);

    // Guest sends a spurious shutdown event.
    DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
           VIRTIO_VSOCK_TYPE_STREAM, packet_op);
    RunLoopUntilIdle();

    // We expect a reset from the host.
    ExpectHostResetOnPort(kVirtioVsockHostPort);
    RunLoopUntilIdle();

    // Ensure that no shutdown events were sent to the endpoints.
    EXPECT_TRUE(received_shutdown_events().empty());
  }
}

TEST_F(VirtioVsockTest, WriteAfterShutdown) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(connection.socket().set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_DISABLED), ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);

  // Test write after shutdown.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort,
         VIRTIO_VSOCK_TYPE_STREAM, VIRTIO_VSOCK_OP_RW);

  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(rx_buffer, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0, VIRTIO_VSOCK_OP_RST, 0);
}

TEST_F(VirtioVsockTest, Read) {
  // Fill a single data buffer in the RxBuffer.
  std::vector<uint8_t> data = {1, 2, 3, 4};
  ASSERT_EQ(data.size(), kDataSize);

  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostReadOnPort(kVirtioVsockHostPort, &connection, data);
  HostReadOnPort(kVirtioVsockHostPort, &connection, data);
}

TEST_F(VirtioVsockTest, ReadChained) {
  // Fill both data buffers in the RxBuffer.
  std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
  ASSERT_EQ(data.size(), 2 * kDataSize);

  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostReadOnPort(kVirtioVsockHostPort, &connection, data);
  HostReadOnPort(kVirtioVsockHostPort, &connection, data);
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
  ASSERT_EQ(connection.socket().write(0, expected.data(), expected.size(), &actual), ZX_OK);
  EXPECT_EQ(actual, expected.size());

  // Expect the guest to pull off |buf_alloc| bytes.
  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(rx_buffer, kVirtioVsockHostPort, kVirtioVsockGuestPort, buf_alloc,
               VIRTIO_VSOCK_OP_RW, 0);
  EXPECT_EQ(memcmp(rx_buffer->data, expected.data(), buf_alloc), 0);

  // Update credit to indicate the in-flight bytes have been free'd.
  fwd_cnt += buf_alloc;
  SendCreditUpdate(kVirtioVsockHostPort, kVirtioVsockGuestPort);

  // Expect to receive the remaining bytes
  rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(rx_buffer, kVirtioVsockHostPort, kVirtioVsockGuestPort, buf_alloc,
               VIRTIO_VSOCK_OP_RW, 0);
  EXPECT_EQ(memcmp(rx_buffer->data, expected.data() + buf_alloc, buf_alloc), 0);
}

TEST_F(VirtioVsockTest, Write) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostWriteOnPort(kVirtioVsockHostPort, &connection);
  HostWriteOnPort(kVirtioVsockHostPort, &connection);
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
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p1), sizeof(p1));
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p2), sizeof(p2));
  RunLoopUntilIdle();

  size_t actual_len = 0;
  uint8_t actual_data[3] = {};
  ASSERT_EQ(connection.socket().read(0, actual_data, sizeof(actual_data), &actual_len), ZX_OK);
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
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p1), sizeof(p1));
  HostQueueWriteOnPort(kVirtioVsockHostPort, reinterpret_cast<uint8_t*>(&p2), sizeof(p2));
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
  ASSERT_EQ(connection.socket().read(0, actual_data, sizeof(actual_data), &actual_len), ZX_OK);
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

  zx_info_socket_t info = {};
  ASSERT_EQ(ZX_OK,
            connection.socket().get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  size_t buf_size = info.tx_buf_max + sizeof(virtio_vsock_hdr_t) + 1;
  auto buf = std::make_unique<uint8_t[]>(buf_size);
  memset(buf.get(), 'a', buf_size);

  // Queue one descriptor that will completely fill the socket (and then some),
  // We'll verify that this resets the connection.
  HostQueueWriteOnPort(kVirtioVsockHostPort, buf.get(), buf_size);
  RunLoopUntilIdle();

  RxBuffer* reset = DoReceive();
  ASSERT_NE(nullptr, reset);
  VerifyHeader(reset, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0, VIRTIO_VSOCK_OP_RST, 0);
}

TEST_F(VirtioVsockTest, SendCreditUpdateWhenSocketIsDrained) {
  TestConnection connection;
  HostConnectOnPortRequest(kVirtioVsockHostPort, &connection);
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  // Fill socket buffer completely.
  zx_info_socket_t info = {};
  ASSERT_EQ(ZX_OK,
            connection.socket().get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr));
  size_t buf_size = info.tx_buf_max + sizeof(virtio_vsock_hdr_t);
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
  ASSERT_EQ(connection.socket().read(0, &byte, 1, &actual_len), ZX_OK);
  ASSERT_EQ(1u, actual_len);
  ASSERT_EQ('a', byte);

  // Verify we get a credit update now that the socket is writable.
  RunLoopUntilIdle();
  RxBuffer* credit_update = DoReceive();
  ASSERT_NE(credit_update, nullptr);
  ASSERT_EQ(info.tx_buf_max, credit_update->header.buf_alloc);
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
    HostReadOnPort(kVirtioVsockHostPort + 1000, &a_connection);
    HostReadOnPort(kVirtioVsockHostPort + 2000, &b_connection);
    HostWriteOnPort(kVirtioVsockHostPort + 1000, &a_connection);
    HostWriteOnPort(kVirtioVsockHostPort + 2000, &b_connection);
  }
}

TEST_F(VirtioVsockTest, InvalidBuffer) {
  zx::socket mock_socket;
  ConnectionKey mock_key{0, 0, 0, 0};
  std::unique_ptr<VirtioVsock::Connection> conn =
      VirtioVsock::Connection::Create(mock_key, std::move(mock_socket), nullptr, nullptr, nullptr);

  conn.get()->SetCredit(0, 2);
  ASSERT_EQ(conn.get()->op(), VIRTIO_VSOCK_OP_RST);
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
  VerifyHeader(rx_buffer, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0,
               VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0);
  EXPECT_GT(rx_buffer->header.buf_alloc, 0u);
  EXPECT_EQ(rx_buffer->header.fwd_cnt, 0u);
}

TEST_F(VirtioVsockTest, UnsupportedSocketType) {
  // Test connection request with invalid type.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestCid, kVirtioVsockGuestPort, UINT16_MAX,
         VIRTIO_VSOCK_OP_REQUEST);

  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  virtio_vsock_hdr_t* rx_header = &rx_buffer->header;
  EXPECT_EQ(rx_header->src_cid, fuchsia::virtualization::HOST_CID);
  EXPECT_EQ(rx_header->dst_cid, kVirtioVsockGuestCid);
  EXPECT_EQ(rx_header->src_port, kVirtioVsockHostPort);
  EXPECT_EQ(rx_header->dst_port, kVirtioVsockGuestPort);
  EXPECT_EQ(rx_header->type, VIRTIO_VSOCK_TYPE_STREAM);
  EXPECT_EQ(rx_header->op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(rx_header->flags, 0u);
}

TEST_F(VirtioVsockTest, InitialCredit) {
  GuestConnectOnPort(kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort, CreateSocket);
  ASSERT_TRUE(remote_handles_.size() == 1);

  zx::socket socket(std::move(remote_handles_.back()));

  std::vector<uint8_t> expected = {1, 2, 3, 4};

  // Write data to socket.
  size_t actual;
  ASSERT_EQ(socket.write(0, expected.data(), expected.size(), &actual), ZX_OK);
  EXPECT_EQ(actual, expected.size());

  // Expect that the connection has correct initial credit and so the guest
  // will be able to pull out the data.
  RxBuffer* rx_buffer = DoReceive();
  ASSERT_NE(nullptr, rx_buffer);
  VerifyHeader(rx_buffer, kVirtioVsockHostPort, kVirtioVsockGuestEphemeralPort, expected.size(),
               VIRTIO_VSOCK_OP_RW, 0);
  EXPECT_EQ(memcmp(rx_buffer->data, expected.data(), expected.size()), 0);
}

TEST(VirtioVsockChain, AllocateAndFree) {
  VirtioDeviceFake device;
  VirtioQueue* queue = device.queue();
  VirtioQueueFake* fake_queue = device.queue_fake();

  // Add an item to the queue.
  virtio_vsock_hdr_t header = {
      .src_port = 1234,
  };
  uint16_t index;
  fake_queue->BuildDescriptor().AppendReadable(&header, sizeof(header)).Build(&index);

  // Ensure we can take the item off the queue and read the header value from it.
  std::optional<VsockChain> chain = VsockChain::FromQueue(queue, /*writable=*/false);
  ASSERT_TRUE(chain.has_value());
  EXPECT_EQ(chain->header()->src_port, 1234u);
  EXPECT_TRUE(!queue->HasAvail());

  // Return the item.
  chain->Return(/*used=*/0);
  EXPECT_TRUE(fake_queue->HasUsed());
}

TEST(VirtioVsockChain, AllocateEmptyQueue) {
  VirtioDeviceFake device;

  // Attempt to take an item off the queue. It should fail.
  std::optional<VsockChain> chain = VsockChain::FromQueue(device.queue(), /*writable=*/false);
  EXPECT_FALSE(chain.has_value());
}

TEST(VirtioVsockChain, AllocateSkipsBadDescriptors) {
  VirtioDeviceFake device;
  VirtioQueue* queue = device.queue();
  VirtioQueueFake* fake_queue = device.queue_fake();

  // Add a too-short descriptor.
  uint8_t byte;
  uint16_t too_small_id;
  fake_queue->BuildDescriptor().AppendReadable(&byte, sizeof(byte)).Build(&too_small_id);

  // Add a writable descriptor when the caller will ask for a readable descriptor.
  virtio_vsock_hdr_t writable_header{};
  uint16_t writable_header_id;
  fake_queue->BuildDescriptor()
      .AppendWritable(&writable_header, sizeof(writable_header))
      .Build(&writable_header_id);

  // Add a valid descriptor.
  virtio_vsock_hdr_t header{
      .src_port = 1234,
  };
  uint16_t valid_id;
  fake_queue->BuildDescriptor().AppendReadable(&header, sizeof(header)).Build(&valid_id);

  // Ensure the valid descriptor was read.
  std::optional<VsockChain> chain = VsockChain::FromQueue(queue, /*writable=*/false);
  ASSERT_TRUE(chain.has_value());
  EXPECT_EQ(chain->header()->src_port, 1234u);

  // Ensure the two invalid descriptors were returned.
  EXPECT_EQ(too_small_id, fake_queue->NextUsed().id);
  EXPECT_EQ(writable_header_id, fake_queue->NextUsed().id);

  chain->Return(/*used=*/0);
}

#endif  // ENABLE_UNSUPPORTED_TESTS

}  // namespace
