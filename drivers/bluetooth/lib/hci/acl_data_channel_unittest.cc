// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/hci/acl_data_channel.h"

#include <unordered_map>

#include "apps/bluetooth/lib/hci/connection.h"
#include "apps/bluetooth/lib/hci/defaults.h"
#include "apps/bluetooth/lib/hci/device_wrapper.h"
#include "apps/bluetooth/lib/hci/fake_controller.h"
#include "apps/bluetooth/lib/hci/transport.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace bluetooth {
namespace hci {
namespace test {
namespace {

class ACLDataChannelTest : public ::testing::Test {
 public:
  ACLDataChannelTest() = default;
  ~ACLDataChannelTest() override = default;

 protected:
  // ::testing::Test overrides:
  void SetUp() override {
    mx::channel cmd0, cmd1;
    mx::channel acl0, acl1;
    mx_status_t status = mx::channel::create(0, &cmd0, &cmd1);
    ASSERT_EQ(NO_ERROR, status);
    status = mx::channel::create(0, &acl0, &acl1);
    ASSERT_EQ(NO_ERROR, status);

    auto hci_dev = std::make_unique<DummyDeviceWrapper>(std::move(cmd0), std::move(acl0));
    transport_ = hci::Transport::Create(std::move(hci_dev));
    fake_controller_ = std::make_unique<FakeController>(std::move(cmd1), std::move(acl1));

    transport_->Initialize();
    fake_controller_->Start();
  }

  void TearDown() override {
    transport_ = nullptr;
    fake_controller_ = nullptr;
  }

  bool InitializeACLDataChannel(const DataBufferInfo& bredr_buffer_info,
                                const DataBufferInfo& le_buffer_info) {
    if (!transport_->InitializeACLDataChannel(
            bredr_buffer_info, le_buffer_info,
            std::bind(&ACLDataChannelTest::LookUpConnection, this, std::placeholders::_1))) {
      return false;
    }

    transport_->acl_data_channel()->SetDataRxHandler(
        std::bind(&ACLDataChannelTest::OnDataReceived, this, std::placeholders::_1),
        message_loop_.task_runner());

    return true;
  }

  void PostDelayedQuitTask(int64_t seconds) {
    message_loop_.task_runner()->PostDelayedTask([this] { message_loop_.PostQuitTask(); },
                                                 ftl::TimeDelta::FromSeconds(seconds));
  }

  void RunMessageLoopWithTimeout(int64_t seconds) {
    // Use PostQuitTask() to queue up the quit task after all pending tasks have been drained.
    PostDelayedQuitTask(seconds);
    message_loop_.Run();
  }

  ftl::RefPtr<Connection> LookUpConnection(ConnectionHandle handle) {
    auto iter = conn_map_.find(handle);
    if (iter == conn_map_.end()) return nullptr;
    return iter->second;
  }

  void OnDataReceived(common::DynamicByteBuffer acl_data_bytes) {
    if (data_received_cb_) data_received_cb_(std::move(acl_data_bytes));
  }

  void AddLEConnection(ConnectionHandle handle) {
    FTL_DCHECK(conn_map_.find(handle) == conn_map_.end());
    // Set some defaults so that all values are non-zero.
    LEConnectionParams params(LEPeerAddressType::kPublic, common::DeviceAddress(),
                              defaults::kLEConnectionIntervalMin,
                              defaults::kLEConnectionIntervalMax,
                              defaults::kLEConnectionIntervalMin,  // conn_interval
                              kLEConnectionLatencyMax,             // conn_latency
                              defaults::kLESupervisionTimeout);
    conn_map_[handle] = Connection::NewLEConnection(handle, Connection::Role::kMaster, params);
  }

  Transport* transport() const { return transport_.get(); }
  CommandChannel* cmd_channel() const { return transport_->command_channel(); }
  ACLDataChannel* acl_data_channel() const { return transport_->acl_data_channel(); }
  FakeController* fake_controller() const { return fake_controller_.get(); }
  mtl::MessageLoop* message_loop() { return &message_loop_; }

  void set_data_received_cb(const ACLDataChannel::DataReceivedCallback& cb) {
    data_received_cb_ = cb;
  }

 private:
  ACLDataChannel::DataReceivedCallback data_received_cb_;
  std::unordered_map<ConnectionHandle, ftl::RefPtr<Connection>> conn_map_;
  ftl::RefPtr<Transport> transport_;
  std::unique_ptr<FakeController> fake_controller_;
  mtl::MessageLoop message_loop_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ACLDataChannelTest);
};

TEST_F(ACLDataChannelTest, VerifyMTUs) {
  const DataBufferInfo kBREDRBufferInfo(1024, 50);
  const DataBufferInfo kLEBufferInfo(64, 16);

  // BR/EDR buffer only.
  InitializeACLDataChannel(kBREDRBufferInfo, DataBufferInfo());
  EXPECT_EQ(kBREDRBufferInfo, acl_data_channel()->GetBufferInfo());
  EXPECT_EQ(kBREDRBufferInfo, acl_data_channel()->GetLEBufferInfo());

  TearDown();
  SetUp();

  // LE buffer only.
  InitializeACLDataChannel(DataBufferInfo(), kLEBufferInfo);
  EXPECT_EQ(DataBufferInfo(), acl_data_channel()->GetBufferInfo());
  EXPECT_EQ(kLEBufferInfo, acl_data_channel()->GetLEBufferInfo());

  TearDown();
  SetUp();

  // Both buffers available.
  InitializeACLDataChannel(kBREDRBufferInfo, kLEBufferInfo);
  EXPECT_EQ(kBREDRBufferInfo, acl_data_channel()->GetBufferInfo());
  EXPECT_EQ(kLEBufferInfo, acl_data_channel()->GetLEBufferInfo());
}

// Test that SendPacket works using the BR/EDR buffer.
TEST_F(ACLDataChannelTest, SendPacketBREDRBuffer) {
  constexpr size_t kMaxMTU = 5;
  constexpr size_t kMaxNumPackets = 5;
  constexpr ConnectionHandle kHandle0 = 0x0001;
  constexpr ConnectionHandle kHandle1 = 0x0002;

  InitializeACLDataChannel(DataBufferInfo(kMaxMTU, kMaxNumPackets), DataBufferInfo());

  // This should fail because the connection doesn't exist.
  common::DynamicByteBuffer buffer(ACLDataTxPacket::GetMinBufferSize(1));
  ACLDataTxPacket packet(kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                         ACLBroadcastFlag::kPointToPoint, 1u, &buffer);
  packet.EncodeHeader();
  EXPECT_FALSE(acl_data_channel()->SendPacket(std::move(buffer)));

  // This should fail because the payload exceeds the MTU.
  AddLEConnection(kHandle0);
  buffer = common::DynamicByteBuffer(ACLDataTxPacket::GetMinBufferSize(kMaxMTU + 1));
  packet = ACLDataTxPacket(kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                           ACLBroadcastFlag::kPointToPoint, kMaxMTU + 1, &buffer);
  packet.EncodeHeader();
  EXPECT_FALSE(acl_data_channel()->SendPacket(std::move(buffer)));

  int handle0_packet_count = 0;
  int handle1_packet_count = 0;
  auto data_callback = [&](const common::ByteBuffer& bytes) {
    ACLDataRxPacket packet(&bytes);
    if (packet.GetConnectionHandle() == kHandle0) {
      handle0_packet_count++;
    } else {
      ASSERT_EQ(kHandle1, packet.GetConnectionHandle());
      handle1_packet_count++;
    }

    if ((handle0_packet_count + handle1_packet_count) % kMaxNumPackets == 0) {
      // We add a 1 second timeout to allow any erroneously sent packets to get through. It's
      // important to do this so that our test isn't guaranteed to succeed if the code has bugs in
      // it.
      PostDelayedQuitTask(1);
    }
  };
  fake_controller()->SetDataCallback(data_callback, message_loop()->task_runner());

  // Correct MTU on kHandle0 should succeed.
  buffer = common::DynamicByteBuffer(ACLDataTxPacket::GetMinBufferSize(kMaxMTU));
  packet = ACLDataTxPacket(kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                           ACLBroadcastFlag::kPointToPoint, kMaxMTU, &buffer);
  packet.EncodeHeader();
  EXPECT_TRUE(acl_data_channel()->SendPacket(std::move(buffer)));

  // Queue up 10 packets in total, distributed among the two connection handles. (1 has been queued
  // on kHandle0 above).
  AddLEConnection(kHandle1);
  for (int i = 0; i < 9; ++i) {
    buffer = common::DynamicByteBuffer(ACLDataTxPacket::GetMinBufferSize(kMaxMTU));
    packet =
        ACLDataTxPacket((i % 2) ? kHandle0 : kHandle1, ACLPacketBoundaryFlag::kFirstNonFlushable,
                        ACLBroadcastFlag::kPointToPoint, kMaxMTU, &buffer);
    packet.EncodeHeader();
    EXPECT_TRUE(acl_data_channel()->SendPacket(std::move(buffer)));
  }

  RunMessageLoopWithTimeout(10);

  // kMaxNumPackets is 5. The controller should have received 3 packets on kHandle0 and 2 on
  // kHandle1
  EXPECT_EQ(3, handle0_packet_count);
  EXPECT_EQ(2, handle1_packet_count);

  // Notify the processed packets with a Number Of Completed Packet HCI event.
  auto event_buffer =
      common::CreateStaticByteBuffer(0x13, 0x09,              // Event header
                                     0x02,                    // Number of handles
                                     0x01, 0x00, 0x03, 0x00,  // 3 packets on handle 0x0001
                                     0x02, 0x00, 0x02, 0x00   // 2 packets on handle 0x0002
                                     );
  fake_controller()->SendCommandChannelPacket(event_buffer);

  RunMessageLoopWithTimeout(10);

  EXPECT_EQ(5, handle0_packet_count);
  EXPECT_EQ(5, handle1_packet_count);
}

// Test that SendPacket works using the LE buffer when no BR/EDR buffer is available.
TEST_F(ACLDataChannelTest, SendPacketLEBuffer) {
  constexpr size_t kLEMaxMTU = 5;
  constexpr size_t kLEMaxNumPackets = 5;
  constexpr size_t kLargeMTU = 6;
  constexpr ConnectionHandle kHandle0 = 0x0001;
  constexpr ConnectionHandle kHandle1 = 0x0002;

  InitializeACLDataChannel(DataBufferInfo(), DataBufferInfo(kLEMaxMTU, kLEMaxNumPackets));
  AddLEConnection(kHandle0);
  AddLEConnection(kHandle1);

  // This should fail because the payload exceeds the LE MTU.
  common::DynamicByteBuffer buffer(ACLDataTxPacket::GetMinBufferSize(kLargeMTU));
  ACLDataTxPacket packet(kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                         ACLBroadcastFlag::kPointToPoint, kLargeMTU, &buffer);
  packet.EncodeHeader();
  EXPECT_FALSE(acl_data_channel()->SendPacket(std::move(buffer)));

  int handle0_packet_count = 0;
  int handle1_packet_count = 0;
  auto data_callback = [&](const common::ByteBuffer& bytes) {
    ACLDataRxPacket packet(&bytes);
    if (packet.GetConnectionHandle() == kHandle0) {
      handle0_packet_count++;
    } else {
      ASSERT_EQ(kHandle1, packet.GetConnectionHandle());
      handle1_packet_count++;
    }

    if ((handle0_packet_count + handle1_packet_count) % kLEMaxNumPackets == 0) {
      // We add a 1 second timeout to allow any erroneously sent packets to get through. It's
      // important to do this so that our test isn't guaranteed to succeed if the code has bugs in
      // it.
      PostDelayedQuitTask(1);
    }
  };
  fake_controller()->SetDataCallback(data_callback, message_loop()->task_runner());

  // Queue up 10 packets in total, distributed among the two connection handles.
  for (int i = 0; i < 10; ++i) {
    buffer = common::DynamicByteBuffer(ACLDataTxPacket::GetMinBufferSize(kLEMaxMTU));
    packet =
        ACLDataTxPacket((i % 2) ? kHandle1 : kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                        ACLBroadcastFlag::kPointToPoint, kLEMaxMTU, &buffer);
    packet.EncodeHeader();
    EXPECT_TRUE(acl_data_channel()->SendPacket(std::move(buffer)));
  }

  RunMessageLoopWithTimeout(10);

  // CommandChannel should be looking at kLEMaxNumPackets, which is 5. The controller should have
  // received 3 packets on kHandle0 and 2 on kHandle1
  EXPECT_EQ(3, handle0_packet_count);
  EXPECT_EQ(2, handle1_packet_count);

  // Notify the processed packets with a Number Of Completed Packet HCI event.
  auto event_buffer =
      common::CreateStaticByteBuffer(0x13, 0x09,              // Event header
                                     0x02,                    // Number of handles
                                     0x01, 0x00, 0x03, 0x00,  // 3 packets on handle 0x0001
                                     0x02, 0x00, 0x02, 0x00   // 2 packets on handle 0x0002
                                     );
  fake_controller()->SendCommandChannelPacket(event_buffer);

  RunMessageLoopWithTimeout(10);

  EXPECT_EQ(5, handle0_packet_count);
  EXPECT_EQ(5, handle1_packet_count);
}

// Test that SendPacket works for LE packets when both buffer types are available.
TEST_F(ACLDataChannelTest, SendPacketBothBuffers) {
  constexpr size_t kMaxMTU = 200;
  constexpr size_t kMaxNumPackets = 50;
  constexpr size_t kLEMaxMTU = 5;
  constexpr size_t kLEMaxNumPackets = 5;
  constexpr ConnectionHandle kHandle0 = 0x0001;
  constexpr ConnectionHandle kHandle1 = 0x0002;

  InitializeACLDataChannel(DataBufferInfo(kMaxMTU, kMaxNumPackets),
                           DataBufferInfo(kLEMaxMTU, kLEMaxNumPackets));
  AddLEConnection(kHandle0);
  AddLEConnection(kHandle1);

  // This should fail because the payload exceeds the LE MTU.
  common::DynamicByteBuffer buffer(ACLDataTxPacket::GetMinBufferSize(kMaxMTU));
  ACLDataTxPacket packet(kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                         ACLBroadcastFlag::kPointToPoint, kMaxMTU, &buffer);
  packet.EncodeHeader();
  EXPECT_FALSE(acl_data_channel()->SendPacket(std::move(buffer)));

  int handle0_packet_count = 0;
  int handle1_packet_count = 0;
  auto data_callback = [&](const common::ByteBuffer& bytes) {
    ACLDataRxPacket packet(&bytes);
    if (packet.GetConnectionHandle() == kHandle0) {
      handle0_packet_count++;
    } else {
      ASSERT_EQ(kHandle1, packet.GetConnectionHandle());
      handle1_packet_count++;
    }

    if ((handle0_packet_count + handle1_packet_count) % kLEMaxNumPackets == 0) {
      // We add a 1 second timeout to allow any erroneously sent packets to get through. It's
      // important to do this so that our test isn't guaranteed to succeed if the code has bugs in
      // it.
      PostDelayedQuitTask(1);
    }
  };
  fake_controller()->SetDataCallback(data_callback, message_loop()->task_runner());

  // Queue up 10 packets in total, distributed among the two connection handles.
  for (int i = 0; i < 10; ++i) {
    buffer = common::DynamicByteBuffer(ACLDataTxPacket::GetMinBufferSize(kLEMaxMTU));
    packet =
        ACLDataTxPacket((i % 2) ? kHandle1 : kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                        ACLBroadcastFlag::kPointToPoint, kLEMaxMTU, &buffer);
    packet.EncodeHeader();
    EXPECT_TRUE(acl_data_channel()->SendPacket(std::move(buffer)));
  }

  RunMessageLoopWithTimeout(10);

  // CommandChannel should be looking at kLEMaxNumPackets, which is 5. The controller should have
  // received 3 packets on kHandle0 and 2 on kHandle1
  EXPECT_EQ(3, handle0_packet_count);
  EXPECT_EQ(2, handle1_packet_count);

  // Notify the processed packets with a Number Of Completed Packet HCI event.
  auto event_buffer =
      common::CreateStaticByteBuffer(0x13, 0x09,              // Event header
                                     0x02,                    // Number of handles
                                     0x01, 0x00, 0x03, 0x00,  // 3 packets on handle 0x0001
                                     0x02, 0x00, 0x02, 0x00   // 2 packets on handle 0x0002
                                     );
  fake_controller()->SendCommandChannelPacket(event_buffer);

  RunMessageLoopWithTimeout(10);

  EXPECT_EQ(5, handle0_packet_count);
  EXPECT_EQ(5, handle1_packet_count);
}

TEST_F(ACLDataChannelTest, SendPacketFromMultipleThreads) {
  constexpr size_t kMaxMTU = 1;
  constexpr size_t kMaxNumPackets = 1;
  constexpr size_t kLEMaxMTU = 5;
  constexpr size_t kLEMaxNumPackets = 5;

  constexpr ConnectionHandle kHandle0 = 0x0001;
  constexpr ConnectionHandle kHandle1 = 0x0002;
  constexpr ConnectionHandle kHandle2 = 0x0003;

  constexpr int kExpectedTotalPacketCount = 18;

  int handle0_total_packet_count = 0;
  int handle1_total_packet_count = 0;
  int handle2_total_packet_count = 0;
  int handle0_processed_count = 0;
  int handle1_processed_count = 0;
  int handle2_processed_count = 0;
  int total_packet_count = 0;
  auto data_cb = [&](const common::ByteBuffer& bytes) {
    ACLDataRxPacket packet(&bytes);
    if (packet.GetConnectionHandle() == kHandle0) {
      handle0_total_packet_count++;
      handle0_processed_count++;
    } else if (packet.GetConnectionHandle() == kHandle1) {
      handle1_total_packet_count++;
      handle1_processed_count++;
    } else {
      ASSERT_EQ(kHandle2, packet.GetConnectionHandle());
      handle2_total_packet_count++;
      handle2_processed_count++;
    }
    total_packet_count++;

    // For every kLEMaxNumPackets packets processed, we notify the host.
    if ((total_packet_count % kLEMaxNumPackets) == 0) {
      auto event_buffer = common::CreateStaticByteBuffer(
          0x13, 0x0D,                                                       // Event header
          0x03,                                                             // Number of handles
          0x01, 0x00, static_cast<uint8_t>(handle0_processed_count), 0x00,  // handle 0x0001
          0x02, 0x00, static_cast<uint8_t>(handle1_processed_count), 0x00,  // handle 0x0002
          0x03, 0x00, static_cast<uint8_t>(handle2_processed_count), 0x00   // handle 0x0003
          );
      handle0_processed_count = 0;
      handle1_processed_count = 0;
      handle2_processed_count = 0;
      fake_controller()->SendCommandChannelPacket(event_buffer);
    }

    if (total_packet_count == kExpectedTotalPacketCount) message_loop()->PostQuitTask();
  };
  fake_controller()->SetDataCallback(data_cb, message_loop()->task_runner());

  InitializeACLDataChannel(DataBufferInfo(kMaxMTU, kMaxNumPackets),
                           DataBufferInfo(kLEMaxMTU, kLEMaxNumPackets));
  AddLEConnection(kHandle0);
  AddLEConnection(kHandle1);
  AddLEConnection(kHandle2);

  // On 3 threads (for each connection handle) we each send 6 packets up to a total of 18.
  auto thread_func = [kLEMaxMTU, this](ConnectionHandle handle) {
    for (int i = 0; i < kExpectedTotalPacketCount / 3; ++i) {
      common::DynamicByteBuffer buffer(ACLDataTxPacket::GetMinBufferSize(kLEMaxMTU));
      ACLDataTxPacket packet(handle, ACLPacketBoundaryFlag::kFirstNonFlushable,
                             ACLBroadcastFlag::kPointToPoint, kLEMaxMTU, &buffer);
      packet.EncodeHeader();
      EXPECT_TRUE(acl_data_channel()->SendPacket(std::move(buffer)));
    }
  };

  // Two have things get processed in real-time we launch the thread as a delayed task on our
  // message loop.
  std::thread t1, t2, t3;
  message_loop()->task_runner()->PostTask([&] {
    t1 = std::thread(thread_func, kHandle0);
    t2 = std::thread(thread_func, kHandle1);
    t3 = std::thread(thread_func, kHandle2);
  });

  // Use a 10 second timeout in case the test fails and the message loop isn't stoppped by |data_cb|
  // above.
  RunMessageLoopWithTimeout(10);

  if (t1.joinable()) t1.join();
  if (t2.joinable()) t2.join();
  if (t3.joinable()) t3.join();

  EXPECT_EQ(kExpectedTotalPacketCount / 3, handle0_total_packet_count);
  EXPECT_EQ(kExpectedTotalPacketCount / 3, handle1_total_packet_count);
  EXPECT_EQ(kExpectedTotalPacketCount / 3, handle2_total_packet_count);
  EXPECT_EQ(kExpectedTotalPacketCount, total_packet_count);
}

TEST_F(ACLDataChannelTest, ReceiveData) {
  constexpr size_t kMaxMTU = 5;
  constexpr size_t kMaxNumPackets = 5;

  // It doesn't matter what we set the buffer values to since we're testing incoming packets.
  InitializeACLDataChannel(DataBufferInfo(kMaxMTU, kMaxNumPackets), DataBufferInfo());

  constexpr size_t kExpectedPacketCount = 2u;
  size_t num_rx_packets = 0u;
  ConnectionHandle packet0_handle;
  ConnectionHandle packet1_handle;
  auto data_rx_cb = [&](common::DynamicByteBuffer bytes) {
    num_rx_packets++;
    if (num_rx_packets == kExpectedPacketCount) message_loop()->PostQuitTask();

    ACLDataRxPacket packet(&bytes);
    if (num_rx_packets == 1)
      packet0_handle = packet.GetConnectionHandle();
    else if (num_rx_packets == 2)
      packet1_handle = packet.GetConnectionHandle();
    else
      FTL_NOTREACHED();
  };
  set_data_received_cb(data_rx_cb);

  // Malformed packet: smaller than the ACL header.
  auto invalid0 = common::CreateStaticByteBuffer(0x01, 0x00, 0x00);

  // Malformed packet: the payload size given in the header doesn't match the actual payload size.
  auto invalid1 = common::CreateStaticByteBuffer(0x01, 0x00, 0x02, 0x00, 0x00);

  // Valid packet on handle 1.
  auto valid0 = common::CreateStaticByteBuffer(0x01, 0x00, 0x01, 0x00, 0x00);

  // Valid packet on handle 2.
  auto valid1 = common::CreateStaticByteBuffer(0x02, 0x00, 0x01, 0x00, 0x00);

  message_loop()->task_runner()->PostTask([&, this] {
    fake_controller()->SendACLDataChannelPacket(invalid0);
    fake_controller()->SendACLDataChannelPacket(invalid1);
    fake_controller()->SendACLDataChannelPacket(valid0);
    fake_controller()->SendACLDataChannelPacket(valid1);
  });

  RunMessageLoopWithTimeout(10);

  EXPECT_EQ(kExpectedPacketCount, num_rx_packets);
  EXPECT_EQ(0x0001, packet0_handle);
  EXPECT_EQ(0x0002, packet1_handle);
}

TEST_F(ACLDataChannelTest, TransportClosedCallback) {
  InitializeACLDataChannel(DataBufferInfo(1u, 1u), DataBufferInfo(1u, 1u));

  bool closed_cb_called = false;
  auto closed_cb = [&closed_cb_called, this] {
    closed_cb_called = true;
    message_loop()->QuitNow();
  };
  transport()->SetTransportClosedCallback(closed_cb, message_loop()->task_runner());

  message_loop()->task_runner()->PostTask([this] { fake_controller()->CloseACLDataChannel(); });
  RunMessageLoopWithTimeout(10);
  EXPECT_TRUE(closed_cb_called);
}

TEST_F(ACLDataChannelTest, TransportClosedCallbackBothChannels) {
  InitializeACLDataChannel(DataBufferInfo(1u, 1u), DataBufferInfo(1u, 1u));

  int closed_cb_count = 0;
  auto closed_cb = [&closed_cb_count, this] { closed_cb_count++; };
  transport()->SetTransportClosedCallback(closed_cb, message_loop()->task_runner());

  // We'll send closed events for both channels. The closed callback should get invoked only once.
  message_loop()->task_runner()->PostTask([this] {
    fake_controller()->CloseACLDataChannel();
    fake_controller()->CloseCommandChannel();
  });

  RunMessageLoopWithTimeout(2);
  EXPECT_EQ(1, closed_cb_count);
}

}  // namespace
}  // namespace test
}  // namespace hci
}  // namespace bluetooth
