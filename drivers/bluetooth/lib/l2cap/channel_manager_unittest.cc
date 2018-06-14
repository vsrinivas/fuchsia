// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel_manager.h"

#include <memory>

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {
namespace {

constexpr hci::ConnectionHandle kTestHandle1 = 0x0001;
constexpr hci::ConnectionHandle kTestHandle2 = 0x0002;

using ::btlib::testing::TestController;

using TestingBase = ::btlib::testing::FakeControllerTest<TestController>;

void DoNothing() {}
void NopRxCallback(const SDU&) {}

class L2CAP_ChannelManagerTest : public TestingBase {
 public:
  L2CAP_ChannelManagerTest() = default;
  ~L2CAP_ChannelManagerTest() override = default;

  void SetUp() override {
    SetUp(hci::DataBufferInfo(hci::kMaxACLPayloadSize, 10),
          hci::DataBufferInfo());
  }

  void SetUp(const hci::DataBufferInfo& acl_info,
             const hci::DataBufferInfo& le_info) {
    TestingBase::SetUp();
    TestingBase::InitializeACLDataChannel(acl_info, le_info);

    // FakeControllerTest's ACL data callbacks will no longer work after this
    // call, as it overwrites ACLDataChannel's data rx handler. This is intended
    // as the L2CAP layer takes ownership of ACL data traffic.
    chanmgr_ = std::make_unique<ChannelManager>(transport(), dispatcher());

    test_device()->StartCmdChannel(test_cmd_chan());
    test_device()->StartAclChannel(test_acl_chan());
  }

  void TearDown() override {
    chanmgr_ = nullptr;
    TestingBase::TearDown();
  }

  fbl::RefPtr<Channel> ActivateNewFixedChannel(
      ChannelId id,
      hci::ConnectionHandle conn_handle = kTestHandle1,
      Channel::ClosedCallback closed_cb = DoNothing,
      Channel::RxCallback rx_cb = NopRxCallback) {
    auto chan = chanmgr()->OpenFixedChannel(conn_handle, id);
    if (!chan ||
        !chan->Activate(std::move(rx_cb), std::move(closed_cb), dispatcher())) {
      return nullptr;
    }

    return chan;
  }

  ChannelManager* chanmgr() const { return chanmgr_.get(); }

 private:
  std::unique_ptr<ChannelManager> chanmgr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(L2CAP_ChannelManagerTest);
};

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelErrorNoConn) {
  // This should fail as the ChannelManager has no entry for |kTestHandle1|.
  EXPECT_EQ(nullptr, ActivateNewFixedChannel(kATTChannelId));

  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  // This should fail as the ChannelManager has no entry for |kTestHandle2|.
  EXPECT_EQ(nullptr, ActivateNewFixedChannel(kATTChannelId, kTestHandle2));
}

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelErrorDisallowedId) {
  // LE-U link
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  // ACL-U link
  chanmgr()->RegisterACL(kTestHandle2, hci::Connection::Role::kMaster,
                         DoNothing, dispatcher());

  // This should fail as kSMPChannelId is ACL-U only.
  EXPECT_EQ(nullptr, ActivateNewFixedChannel(kSMPChannelId, kTestHandle1));

  // This should fail as kATTChannelId is LE-U only.
  EXPECT_EQ(nullptr, ActivateNewFixedChannel(kATTChannelId, kTestHandle2));
}

TEST_F(L2CAP_ChannelManagerTest, ActivateFailsAfterDeactivate) {
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());
  auto chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1);
  ASSERT_TRUE(chan);

  chan->Deactivate();

  // Activate should fail.
  EXPECT_FALSE(chan->Activate(NopRxCallback, DoNothing, dispatcher()));
}

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelAndUnregisterLink) {
  // LE-U link
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  bool closed_called = false;
  auto closed_cb = [&closed_called] { closed_called = true; };

  auto chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1, closed_cb);
  ASSERT_TRUE(chan);
  EXPECT_EQ(kTestHandle1, chan->link_handle());

  // This should notify the channel.
  chanmgr()->Unregister(kTestHandle1);

  RunLoopUntilIdle();

  // |closed_cb| will be called synchronously since it was registered using the
  // current thread's task runner.
  EXPECT_TRUE(closed_called);
}

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelAndCloseChannel) {
  // LE-U link
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  bool closed_called = false;
  auto closed_cb = [&closed_called] { closed_called = true; };

  auto chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1, closed_cb);
  ASSERT_TRUE(chan);

  // Close the channel before unregistering the link. |closed_cb| should not get
  // called.
  chan->Deactivate();
  chanmgr()->Unregister(kTestHandle1);

  RunLoopUntilIdle();

  EXPECT_FALSE(closed_called);
}

TEST_F(L2CAP_ChannelManagerTest, OpenAndCloseMultipleFixedChannels) {
  // LE-U link
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  bool att_closed = false;
  auto att_closed_cb = [&att_closed] { att_closed = true; };

  bool smp_closed = false;
  auto smp_closed_cb = [&smp_closed] { smp_closed = true; };

  auto att_chan =
      ActivateNewFixedChannel(kATTChannelId, kTestHandle1, att_closed_cb);
  ASSERT_TRUE(att_chan);

  auto smp_chan =
      ActivateNewFixedChannel(kLESMPChannelId, kTestHandle1, smp_closed_cb);
  ASSERT_TRUE(smp_chan);

  smp_chan->Deactivate();
  chanmgr()->Unregister(kTestHandle1);

  RunLoopUntilIdle();

  EXPECT_TRUE(att_closed);
  EXPECT_FALSE(smp_closed);
}

TEST_F(L2CAP_ChannelManagerTest, ReceiveData) {
  // LE-U link
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  common::StaticByteBuffer<255> buffer;

  // We use the ATT channel to control incoming packets and the SMP channel to
  // quit the message loop.
  std::vector<std::string> sdus;
  auto att_rx_cb = [&sdus, &buffer](const SDU& sdu) {
    size_t size = sdu.Copy(&buffer);
    sdus.push_back(buffer.view(0, size).ToString());
  };

  bool smp_cb_called = false;
  auto smp_rx_cb = [&smp_cb_called, this](const SDU& sdu) {
    EXPECT_EQ(0u, sdu.length());
    smp_cb_called = true;
  };

  auto att_chan =
      ActivateNewFixedChannel(kATTChannelId, kTestHandle1, [] {}, att_rx_cb);
  auto smp_chan =
      ActivateNewFixedChannel(kLESMPChannelId, kTestHandle1, [] {}, smp_rx_cb);
  ASSERT_TRUE(att_chan);
  ASSERT_TRUE(smp_chan);

  // ATT channel
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (starting fragment)
      0x01, 0x00, 0x09, 0x00,

      // L2CAP B-frame
      0x05, 0x00, 0x04, 0x00, 'h', 'e', 'l', 'l', 'o'));
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (starting fragment)
      0x01, 0x00, 0x09, 0x00,

      // L2CAP B-frame (partial)
      0x0C, 0x00, 0x04, 0x00, 'h', 'o', 'w', ' ', 'a'));
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (continuing fragment)
      0x01, 0x10, 0x07, 0x00,

      // L2CAP B-frame (partial)
      'r', 'e', ' ', 'y', 'o', 'u', '?'));

  // SMP channel
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (starting fragment)
      0x01, 0x00, 0x04, 0x00,

      // L2CAP B-frame (empty)
      0x00, 0x00, 0x06, 0x00));

  RunLoopUntilIdle();

  EXPECT_TRUE(smp_cb_called);
  ASSERT_EQ(2u, sdus.size());
  EXPECT_EQ("hello", sdus[0]);
  EXPECT_EQ("how are you?", sdus[1]);
}

TEST_F(L2CAP_ChannelManagerTest, ReceiveDataBeforeRegisteringLink) {
  constexpr size_t kPacketCount = 10;

  common::StaticByteBuffer<255> buffer;

  // We use the ATT channel to control incoming packets and the SMP channel to
  // quit the message loop.
  size_t packet_count = 0;
  auto att_rx_cb = [&packet_count](const SDU& sdu) { packet_count++; };

  bool smp_cb_called = false;
  auto smp_rx_cb = [&smp_cb_called, this](const SDU& sdu) {
    EXPECT_EQ(0u, sdu.length());
    smp_cb_called = true;
  };

  // ATT channel
  for (size_t i = 0u; i < kPacketCount; i++) {
    test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
        // ACL data header (starting fragment)
        0x01, 0x00, 0x04, 0x00,

        // L2CAP B-frame
        0x00, 0x00, 0x04, 0x00));
  }

  // SMP channel
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (starting fragment)
      0x01, 0x00, 0x04, 0x00,

      // L2CAP B-frame (empty)
      0x00, 0x00, 0x06, 0x00));

  fbl::RefPtr<Channel> att_chan, smp_chan;

  // Run the loop so all packets are received.
  RunLoopUntilIdle();

  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  att_chan =
      ActivateNewFixedChannel(kATTChannelId, kTestHandle1, [] {}, att_rx_cb);
  FXL_DCHECK(att_chan);

  smp_chan =
      ActivateNewFixedChannel(kLESMPChannelId, kTestHandle1, [] {}, smp_rx_cb);
  FXL_DCHECK(smp_chan);

  RunLoopUntilIdle();
  EXPECT_TRUE(smp_cb_called);
  EXPECT_EQ(kPacketCount, packet_count);
}

// Receive data after registering the link but before creating the channel.
TEST_F(L2CAP_ChannelManagerTest, ReceiveDataBeforeCreatingChannel) {
  constexpr size_t kPacketCount = 10;

  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());

  common::StaticByteBuffer<255> buffer;

  // We use the ATT channel to control incoming packets and the SMP channel to
  // quit the message loop.
  size_t packet_count = 0;
  auto att_rx_cb = [&packet_count](const SDU& sdu) { packet_count++; };

  bool smp_cb_called = false;
  auto smp_rx_cb = [&smp_cb_called, this](const SDU& sdu) {
    EXPECT_EQ(0u, sdu.length());
    smp_cb_called = true;
  };

  // ATT channel
  for (size_t i = 0u; i < kPacketCount; i++) {
    test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
        // ACL data header (starting fragment)
        0x01, 0x00, 0x04, 0x00,

        // L2CAP B-frame
        0x00, 0x00, 0x04, 0x00));
  }

  // SMP channel
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (starting fragment)
      0x01, 0x00, 0x04, 0x00,

      // L2CAP B-frame (empty)
      0x00, 0x00, 0x06, 0x00));

  fbl::RefPtr<Channel> att_chan, smp_chan;

  // Run the loop so all packets are received.
  RunLoopUntilIdle();

  att_chan =
      ActivateNewFixedChannel(kATTChannelId, kTestHandle1, [] {}, att_rx_cb);
  FXL_DCHECK(att_chan);

  smp_chan =
      ActivateNewFixedChannel(kLESMPChannelId, kTestHandle1, [] {}, smp_rx_cb);
  FXL_DCHECK(smp_chan);

  RunLoopUntilIdle();

  EXPECT_TRUE(smp_cb_called);
  EXPECT_EQ(kPacketCount, packet_count);
}

// Receive data after registering the link and creating the channel but before
// setting the rx handler.
TEST_F(L2CAP_ChannelManagerTest, ReceiveDataBeforeSettingRxHandler) {
  constexpr size_t kPacketCount = 10;

  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());
  auto att_chan = chanmgr()->OpenFixedChannel(kTestHandle1, kATTChannelId);
  FXL_DCHECK(att_chan);

  auto smp_chan = chanmgr()->OpenFixedChannel(kTestHandle1, kLESMPChannelId);
  FXL_DCHECK(smp_chan);

  common::StaticByteBuffer<255> buffer;

  // We use the ATT channel to control incoming packets and the SMP channel to
  // quit the message loop.
  size_t packet_count = 0;
  auto att_rx_cb = [&packet_count](const SDU& sdu) { packet_count++; };

  bool smp_cb_called = false;
  auto smp_rx_cb = [&smp_cb_called, this](const SDU& sdu) {
    EXPECT_EQ(0u, sdu.length());
    smp_cb_called = true;
  };

  // ATT channel
  for (size_t i = 0u; i < kPacketCount; i++) {
    test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
        // ACL data header (starting fragment)
        0x01, 0x00, 0x04, 0x00,

        // L2CAP B-frame
        0x00, 0x00, 0x04, 0x00));
  }

  // SMP channel
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (starting fragment)
      0x01, 0x00, 0x04, 0x00,

      // L2CAP B-frame (empty)
      0x00, 0x00, 0x06, 0x00));

  // Run the loop so all packets are received.
  RunLoopUntilIdle();

  att_chan->Activate(att_rx_cb, DoNothing, dispatcher());
  smp_chan->Activate(smp_rx_cb, DoNothing, dispatcher());

  RunLoopUntilIdle();

  EXPECT_TRUE(smp_cb_called);
  EXPECT_EQ(kPacketCount, packet_count);
}

TEST_F(L2CAP_ChannelManagerTest, SendOnClosedLink) {
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());
  auto att_chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1);
  FXL_DCHECK(att_chan);

  chanmgr()->Unregister(kTestHandle1);

  EXPECT_FALSE(att_chan->Send(common::NewBuffer('T', 'e', 's', 't')));
}

TEST_F(L2CAP_ChannelManagerTest, SendBasicSdu) {
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());
  auto att_chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1);
  FXL_DCHECK(att_chan);

  std::unique_ptr<common::ByteBuffer> received;
  auto data_cb = [&received](const common::ByteBuffer& bytes) {
    received = std::make_unique<common::DynamicByteBuffer>(bytes);
  };
  test_device()->SetDataCallback(data_cb, dispatcher());

  EXPECT_TRUE(att_chan->Send(common::NewBuffer('T', 'e', 's', 't')));

  RunLoopUntilIdle();
  ASSERT_TRUE(received);

  auto expected = common::CreateStaticByteBuffer(
      // ACL data header (handle: 1, length 7)
      0x01, 0x00, 0x08, 0x00,

      // L2CAP B-frame: (length: 3, channel-id: 4)
      0x04, 0x00, 0x04, 0x00, 'T', 'e', 's', 't');

  EXPECT_TRUE(common::ContainersEqual(expected, *received));
}

// Tests that fragmentation of LE vs BR/EDR packets is based on the same
// fragment size.
TEST_F(L2CAP_ChannelManagerTest, SendFragmentedSdus) {
  constexpr size_t kMaxNumPackets =
      100;  // Make this large to avoid simulating flow-control.
  constexpr size_t kMaxDataSize = 5;
  //constexpr size_t kExpectedNumFragments = 5;

  // No LE buffers.
  TearDown();
  SetUp(hci::DataBufferInfo(kMaxDataSize, kMaxNumPackets),
        hci::DataBufferInfo());

  std::vector<std::unique_ptr<common::ByteBuffer>> le_fragments, acl_fragments;
  auto data_cb = [&le_fragments,
                  &acl_fragments](const common::ByteBuffer& bytes) {
    FXL_DCHECK(bytes.size() >= sizeof(hci::ACLDataHeader));

    common::PacketView<hci::ACLDataHeader> packet(
        &bytes, bytes.size() - sizeof(hci::ACLDataHeader));
    hci::ConnectionHandle handle =
        le16toh(packet.header().handle_and_flags) & 0xFFF;

    if (handle == kTestHandle1)
      le_fragments.push_back(
          std::make_unique<common::DynamicByteBuffer>(bytes));
    else if (handle == kTestHandle2)
      acl_fragments.push_back(
          std::make_unique<common::DynamicByteBuffer>(bytes));
  };
  test_device()->SetDataCallback(data_cb, dispatcher());

  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());
  chanmgr()->RegisterACL(kTestHandle2, hci::Connection::Role::kMaster,
                         DoNothing, dispatcher());

  // We use the ATT fixed-channel for LE and the SM fixed-channel for ACL.
  auto att_chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1);
  auto sm_chan = ActivateNewFixedChannel(kSMPChannelId, kTestHandle2);
  ASSERT_TRUE(att_chan);
  ASSERT_TRUE(sm_chan);

  // SDU of length 5 corresponds to a 9-octet B-frame which should be sent over
  // 2 fragments.
  EXPECT_TRUE(att_chan->Send(common::NewBuffer('H', 'e', 'l', 'l', 'o')));

  // SDU of length 7 corresponds to a 11-octet B-frame which should be sent over
  // 3 fragments.
  EXPECT_TRUE(
      sm_chan->Send(common::NewBuffer('G', 'o', 'o', 'd', 'b', 'y', 'e')));

  RunLoopUntilIdle();

  EXPECT_EQ(2u, le_fragments.size());
  ASSERT_EQ(3u, acl_fragments.size());

  auto expected_le_0 = common::CreateStaticByteBuffer(
      // ACL data header (handle: 1, length: 5)
      0x01, 0x00, 0x05, 0x00,

      // L2CAP B-frame: (length: 5, channel-id: 4, partial payload)
      0x05, 0x00, 0x04, 0x00, 'H');

  auto expected_le_1 = common::CreateStaticByteBuffer(
      // ACL data header (handle: 1, pbf: continuing fr., length: 4)
      0x01, 0x10, 0x04, 0x00,

      // Continuing payload
      'e', 'l', 'l', 'o');

  auto expected_acl_0 = common::CreateStaticByteBuffer(
      // ACL data header (handle: 2, length: 5)
      0x02, 0x00, 0x05, 0x00,

      // l2cap b-frame: (length: 7, channel-id: 7, partial payload)
      0x07, 0x00, 0x07, 0x00, 'G');

  auto expected_acl_1 = common::CreateStaticByteBuffer(
      // ACL data header (handle: 2, pbf: continuing fr., length: 5)
      0x02, 0x10, 0x05, 0x00,

      // continuing payload
      'o', 'o', 'd', 'b', 'y');

  auto expected_acl_2 = common::CreateStaticByteBuffer(
      // ACL data header (handle: 2, pbf: continuing fr., length: 1)
      0x02, 0x10, 0x01, 0x00,

      // Continuing payload
      'e');

  EXPECT_TRUE(common::ContainersEqual(expected_le_0, *le_fragments[0]));
  EXPECT_TRUE(common::ContainersEqual(expected_le_1, *le_fragments[1]));

  EXPECT_TRUE(common::ContainersEqual(expected_acl_0, *acl_fragments[0]));
  EXPECT_TRUE(common::ContainersEqual(expected_acl_1, *acl_fragments[1]));
  EXPECT_TRUE(common::ContainersEqual(expected_acl_2, *acl_fragments[2]));
}

// Tests that fragmentation of LE and BR/EDR packets use the corresponding
// buffer size.
TEST_F(L2CAP_ChannelManagerTest, SendFragmentedSdusDifferentBuffers) {
  constexpr size_t kMaxNumPackets =
      100;  // This is large to avoid having to simulate flow-control
  constexpr size_t kMaxACLDataSize = 6;
  constexpr size_t kMaxLEDataSize = 10;
  //constexpr size_t kExpectedNumFragments = 3;

  TearDown();
  SetUp(hci::DataBufferInfo(kMaxACLDataSize, kMaxNumPackets),
        hci::DataBufferInfo(kMaxLEDataSize, kMaxNumPackets));

  std::vector<std::unique_ptr<common::ByteBuffer>> le_fragments, acl_fragments;
  auto data_cb = [&le_fragments,
                  &acl_fragments](const common::ByteBuffer& bytes) {
    FXL_DCHECK(bytes.size() >= sizeof(hci::ACLDataHeader));

    common::PacketView<hci::ACLDataHeader> packet(
        &bytes, bytes.size() - sizeof(hci::ACLDataHeader));
    hci::ConnectionHandle handle =
        le16toh(packet.header().handle_and_flags) & 0xFFF;

    if (handle == kTestHandle1)
      le_fragments.push_back(
          std::make_unique<common::DynamicByteBuffer>(bytes));
    else if (handle == kTestHandle2)
      acl_fragments.push_back(
          std::make_unique<common::DynamicByteBuffer>(bytes));
  };
  test_device()->SetDataCallback(data_cb, dispatcher());

  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, DoNothing, dispatcher());
  chanmgr()->RegisterACL(kTestHandle2, hci::Connection::Role::kMaster,
                         DoNothing, dispatcher());

  // We use the ATT fixed-channel for LE and the SM fixed-channel for ACL.
  auto att_chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1);
  auto sm_chan = ActivateNewFixedChannel(kSMPChannelId, kTestHandle2);
  ASSERT_TRUE(att_chan);
  ASSERT_TRUE(sm_chan);

  // SDU of length 5 corresponds to a 9-octet B-frame. The LE buffer size is
  // large enough for this to be sent over a single fragment.
  EXPECT_TRUE(att_chan->Send(common::NewBuffer('H', 'e', 'l', 'l', 'o')));

  // SDU of length 7 corresponds to a 11-octet B-frame. Due to the BR/EDR buffer
  // size, this should be sent over 2 fragments.
  EXPECT_TRUE(
      sm_chan->Send(common::NewBuffer('G', 'o', 'o', 'd', 'b', 'y', 'e')));

  RunLoopUntilIdle();

  EXPECT_EQ(1u, le_fragments.size());
  ASSERT_EQ(2u, acl_fragments.size());

  auto expected_le = common::CreateStaticByteBuffer(
      // ACL data header (handle: 1, length: 9)
      0x01, 0x00, 0x09, 0x00,

      // L2CAP B-frame: (length: 5, channel-id: 4)
      0x05, 0x00, 0x04, 0x00, 'H', 'e', 'l', 'l', 'o');

  auto expected_acl_0 = common::CreateStaticByteBuffer(
      // ACL data header (handle: 2, length: 6)
      0x02, 0x00, 0x06, 0x00,

      // l2cap b-frame: (length: 7, channel-id: 7, partial payload)
      0x07, 0x00, 0x07, 0x00, 'G', 'o');

  auto expected_acl_1 = common::CreateStaticByteBuffer(
      // ACL data header (handle: 2, pbf: continuing fr., length: 5)
      0x02, 0x10, 0x05, 0x00,

      // continuing payload
      'o', 'd', 'b', 'y', 'e');

  EXPECT_TRUE(common::ContainersEqual(expected_le, *le_fragments[0]));

  EXPECT_TRUE(common::ContainersEqual(expected_acl_0, *acl_fragments[0]));
  EXPECT_TRUE(common::ContainersEqual(expected_acl_1, *acl_fragments[1]));
}

TEST_F(L2CAP_ChannelManagerTest, LEChannelSignalLinkError) {
  bool link_error = false;
  auto link_error_cb = [&link_error, this] {
    link_error = true;
  };
  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        [](auto) {}, link_error_cb, dispatcher());

  // Activate a new Attribute channel to signal the error.
  auto chan = ActivateNewFixedChannel(kATTChannelId, kTestHandle1);
  chan->SignalLinkError();

  // The event will run asynchronously.
  EXPECT_FALSE(link_error);

  RunLoopUntilIdle();
  EXPECT_TRUE(link_error);
}

TEST_F(L2CAP_ChannelManagerTest, ACLChannelSignalLinkError) {
  bool link_error = false;
  auto link_error_cb = [&link_error, this] {
    link_error = true;
  };
  chanmgr()->RegisterACL(kTestHandle1, hci::Connection::Role::kMaster,
                         link_error_cb, dispatcher());

  // Activate a new Security Manager channel to signal the error.
  auto chan = ActivateNewFixedChannel(kSMPChannelId, kTestHandle1);
  chan->SignalLinkError();

  // The event will run asynchronously.
  EXPECT_FALSE(link_error);

  RunLoopUntilIdle();
  EXPECT_TRUE(link_error);
}

TEST_F(L2CAP_ChannelManagerTest, LEConnectionParameterUpdateRequest) {
  bool conn_param_cb_called = false;
  auto conn_param_cb = [&conn_param_cb_called, this](const auto& params) {
    // The parameters should match the payload of the HCI packet seen below.
    EXPECT_EQ(0x0006, params.min_interval());
    EXPECT_EQ(0x0C80, params.max_interval());
    EXPECT_EQ(0x01F3, params.max_latency());
    EXPECT_EQ(0x0C80, params.supervision_timeout());
    conn_param_cb_called = true;
  };

  chanmgr()->RegisterLE(kTestHandle1, hci::Connection::Role::kMaster,
                        conn_param_cb, DoNothing, dispatcher());

  // clang-format off
  test_device()->SendACLDataChannelPacket(common::CreateStaticByteBuffer(
      // ACL data header (handle: 0x0001, length: 16 bytes)
      0x01, 0x00, 0x10, 0x00,

      // L2CAP B-frame header (length: 12 bytes, channel-id: 0x0005 (LE sig))
      0x0C, 0x00, 0x05, 0x00,

      // L2CAP C-frame header
      // (LE conn. param. update request, id: 1, length: 8 bytes)
      0x12, 0x01, 0x08, 0x00,

      // Connection parameters (hardcoded to match the expections in
      // |conn_param_cb|).
      0x06, 0x00,
      0x80, 0x0C,
      0xF3, 0x01,
      0x80, 0x0C));
  // clang-format on

  RunLoopUntilIdle();
  EXPECT_TRUE(conn_param_cb_called);
}

}  // namespace
}  // namespace l2cap
}  // namespace btlib
