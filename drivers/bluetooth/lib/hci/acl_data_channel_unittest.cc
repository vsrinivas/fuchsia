// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/hci/acl_data_channel.h"

#include <unordered_map>

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/hci/connection.h"
#include "apps/bluetooth/lib/hci/defaults.h"
#include "apps/bluetooth/lib/hci/test_base.h"
#include "apps/bluetooth/lib/hci/test_controller.h"
#include "apps/bluetooth/lib/hci/transport.h"

namespace bluetooth {
namespace hci {
namespace test {
namespace {

class ACLDataChannelTest : public TransportTest<TestController> {
 public:
  ACLDataChannelTest() = default;
  ~ACLDataChannelTest() override = default;

 protected:
  // ::testing::Test overrides:
  void SetUp() override {
    TransportTest<TestController>::SetUp();

    // This test never sets up command/event expections (as it only uses the data endpoint) so
    // always start the test controller during SetUp().
    test_device()->Start();
  }

  void AddLEConnection(ConnectionHandle handle) {
    FTL_DCHECK(conn_map_.find(handle) == conn_map_.end());
    // Set some defaults so that all values are non-zero.
    LEConnectionParams params(LEPeerAddressType::kPublic, common::DeviceAddressBytes(),
                              defaults::kLEConnectionIntervalMin,
                              defaults::kLEConnectionIntervalMax,
                              defaults::kLEConnectionIntervalMin,  // conn_interval
                              kLEConnectionLatencyMax,             // conn_latency
                              defaults::kLESupervisionTimeout);
    conn_map_[handle] = Connection::NewLEConnection(handle, Connection::Role::kMaster, params);
  }

  void SetUpConnectionLookUpCallback() {
    set_connection_lookup_callback(
        std::bind(&ACLDataChannelTest::LookUpConnection, this, std::placeholders::_1));
  }

 private:
  ftl::RefPtr<Connection> LookUpConnection(ConnectionHandle handle) {
    auto iter = conn_map_.find(handle);
    if (iter == conn_map_.end()) return nullptr;
    return iter->second;
  }

  std::unordered_map<ConnectionHandle, ftl::RefPtr<Connection>> conn_map_;

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
  SetUpConnectionLookUpCallback();

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
  test_device()->SetDataCallback(data_callback, message_loop()->task_runner());

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

  RunMessageLoop(10);

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
  test_device()->SendCommandChannelPacket(event_buffer);

  RunMessageLoop(10);

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
  SetUpConnectionLookUpCallback();
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
  test_device()->SetDataCallback(data_callback, message_loop()->task_runner());

  // Queue up 10 packets in total, distributed among the two connection handles.
  for (int i = 0; i < 10; ++i) {
    buffer = common::DynamicByteBuffer(ACLDataTxPacket::GetMinBufferSize(kLEMaxMTU));
    packet =
        ACLDataTxPacket((i % 2) ? kHandle1 : kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                        ACLBroadcastFlag::kPointToPoint, kLEMaxMTU, &buffer);
    packet.EncodeHeader();
    EXPECT_TRUE(acl_data_channel()->SendPacket(std::move(buffer)));
  }

  RunMessageLoop(10);

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
  test_device()->SendCommandChannelPacket(event_buffer);

  RunMessageLoop(10);

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
  SetUpConnectionLookUpCallback();
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
  test_device()->SetDataCallback(data_callback, message_loop()->task_runner());

  // Queue up 10 packets in total, distributed among the two connection handles.
  for (int i = 0; i < 10; ++i) {
    buffer = common::DynamicByteBuffer(ACLDataTxPacket::GetMinBufferSize(kLEMaxMTU));
    packet =
        ACLDataTxPacket((i % 2) ? kHandle1 : kHandle0, ACLPacketBoundaryFlag::kFirstNonFlushable,
                        ACLBroadcastFlag::kPointToPoint, kLEMaxMTU, &buffer);
    packet.EncodeHeader();
    EXPECT_TRUE(acl_data_channel()->SendPacket(std::move(buffer)));
  }

  RunMessageLoop(10);

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
  test_device()->SendCommandChannelPacket(event_buffer);

  RunMessageLoop(10);

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
      test_device()->SendCommandChannelPacket(event_buffer);
    }

    if (total_packet_count == kExpectedTotalPacketCount) message_loop()->PostQuitTask();
  };
  test_device()->SetDataCallback(data_cb, message_loop()->task_runner());

  InitializeACLDataChannel(DataBufferInfo(kMaxMTU, kMaxNumPackets),
                           DataBufferInfo(kLEMaxMTU, kLEMaxNumPackets));
  SetUpConnectionLookUpCallback();
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
  RunMessageLoop(10);

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
  SetUpConnectionLookUpCallback();

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
  set_data_received_callback(data_rx_cb);

  // Malformed packet: smaller than the ACL header.
  auto invalid0 = common::CreateStaticByteBuffer(0x01, 0x00, 0x00);

  // Malformed packet: the payload size given in the header doesn't match the actual payload size.
  auto invalid1 = common::CreateStaticByteBuffer(0x01, 0x00, 0x02, 0x00, 0x00);

  // Valid packet on handle 1.
  auto valid0 = common::CreateStaticByteBuffer(0x01, 0x00, 0x01, 0x00, 0x00);

  // Valid packet on handle 2.
  auto valid1 = common::CreateStaticByteBuffer(0x02, 0x00, 0x01, 0x00, 0x00);

  message_loop()->task_runner()->PostTask([&, this] {
    test_device()->SendACLDataChannelPacket(invalid0);
    test_device()->SendACLDataChannelPacket(invalid1);
    test_device()->SendACLDataChannelPacket(valid0);
    test_device()->SendACLDataChannelPacket(valid1);
  });

  RunMessageLoop(10);

  EXPECT_EQ(kExpectedPacketCount, num_rx_packets);
  EXPECT_EQ(0x0001, packet0_handle);
  EXPECT_EQ(0x0002, packet1_handle);
}

TEST_F(ACLDataChannelTest, TransportClosedCallback) {
  InitializeACLDataChannel(DataBufferInfo(1u, 1u), DataBufferInfo(1u, 1u));
  SetUpConnectionLookUpCallback();

  bool closed_cb_called = false;
  auto closed_cb = [&closed_cb_called, this] {
    closed_cb_called = true;
    message_loop()->QuitNow();
  };
  transport()->SetTransportClosedCallback(closed_cb, message_loop()->task_runner());

  message_loop()->task_runner()->PostTask([this] { test_device()->CloseACLDataChannel(); });
  RunMessageLoop(10);
  EXPECT_TRUE(closed_cb_called);
}

TEST_F(ACLDataChannelTest, TransportClosedCallbackBothChannels) {
  InitializeACLDataChannel(DataBufferInfo(1u, 1u), DataBufferInfo(1u, 1u));
  SetUpConnectionLookUpCallback();

  int closed_cb_count = 0;
  auto closed_cb = [&closed_cb_count, this] { closed_cb_count++; };
  transport()->SetTransportClosedCallback(closed_cb, message_loop()->task_runner());

  // We'll send closed events for both channels. The closed callback should get invoked only once.
  message_loop()->task_runner()->PostTask([this] {
    test_device()->CloseACLDataChannel();
    test_device()->CloseCommandChannel();
  });

  RunMessageLoop(2);
  EXPECT_EQ(1, closed_cb_count);
}

}  // namespace
}  // namespace test
}  // namespace hci
}  // namespace bluetooth
