// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel_manager.h"

#include <memory>

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/hci/connection.h"
#include "apps/bluetooth/lib/testing/fake_controller.h"
#include "apps/bluetooth/lib/testing/test_base.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/threading/create_thread.h"

namespace bluetooth {
namespace l2cap {
namespace {

constexpr hci::ConnectionHandle kTestHandle1 = 0x0001;
constexpr hci::ConnectionHandle kTestHandle2 = 0x0002;

using ::bluetooth::testing::FakeController;

using TestingBase = ::bluetooth::testing::TransportTest<FakeController>;

class L2CAP_ChannelManagerTest : public TestingBase {
 public:
  L2CAP_ChannelManagerTest() = default;
  ~L2CAP_ChannelManagerTest() override = default;

  void SetUp() override {
    TestingBase::SetUp();
    TestingBase::InitializeACLDataChannel(
        hci::DataBufferInfo(hci::kMaxACLPayloadSize + sizeof(hci::ACLDataHeader), 10),
        hci::DataBufferInfo());

    // TransportTest's ACL data callbacks will no longer work after this call, as it overwrites
    // ACLDataChannel's data rx handler. This is intended as the L2CAP layer takes ownership of ACL
    // data traffic.
    chanmgr_ = std::make_unique<ChannelManager>(transport(), message_loop()->task_runner());
  }

  void TearDown() override {
    chanmgr_ = nullptr;
    TestingBase::TearDown();
  }

  std::unique_ptr<Channel> OpenFixedChannel(ChannelId id,
                                            hci::ConnectionHandle conn_handle = kTestHandle1,
                                            const Channel::ClosedCallback& closed_cb = {},
                                            const Channel::RxCallback& rx_cb = {}) {
    auto chan = chanmgr()->OpenFixedChannel(conn_handle, id);
    if (chan) {
      chan->set_channel_closed_callback(closed_cb);
      chan->SetRxHandler(rx_cb, rx_cb ? message_loop()->task_runner() : nullptr);
    }

    return chan;
  }

  ChannelManager* chanmgr() const { return chanmgr_.get(); }

 private:
  std::unique_ptr<ChannelManager> chanmgr_;

  FTL_DISALLOW_COPY_AND_ASSIGN(L2CAP_ChannelManagerTest);
};

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelErrorNoConn) {
  // This should fail as the ChannelManager has no entry for |kTestHandle1|.
  EXPECT_EQ(nullptr, OpenFixedChannel(kATTChannelId));

  // This should fail as the ChannelManager has no entry for |kTestHandle2|.
  chanmgr()->Register(kTestHandle1, hci::Connection::LinkType::kLE, hci::Connection::Role::kMaster);
  EXPECT_EQ(nullptr, OpenFixedChannel(kATTChannelId, kTestHandle2));
}

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelErrorDisallowedId) {
  // LE-U link
  chanmgr()->Register(kTestHandle1, hci::Connection::LinkType::kLE, hci::Connection::Role::kMaster);

  // ACL-U link
  chanmgr()->Register(kTestHandle2, hci::Connection::LinkType::kACL,
                      hci::Connection::Role::kMaster);

  // This should fail as kSMChannelId is ACL-U only.
  EXPECT_EQ(nullptr, OpenFixedChannel(kSMChannelId, kTestHandle1));

  // This should fail as kATTChannelId is LE-U only.
  EXPECT_EQ(nullptr, OpenFixedChannel(kATTChannelId, kTestHandle2));
}

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelAndUnregisterLink) {
  // LE-U link
  chanmgr()->Register(kTestHandle1, hci::Connection::LinkType::kLE, hci::Connection::Role::kMaster);

  bool closed_called = false;
  auto closed_cb = [&closed_called] { closed_called = true; };

  auto chan = OpenFixedChannel(kATTChannelId, kTestHandle1, closed_cb);
  ASSERT_TRUE(chan);

  // This should notify the channel.
  chanmgr()->Unregister(kTestHandle1);

  // |closed_cb| will be called synchronously since it was registered using the current thread's
  // task runner.
  EXPECT_TRUE(closed_called);
}

TEST_F(L2CAP_ChannelManagerTest, OpenFixedChannelAndCloseChannel) {
  // LE-U link
  chanmgr()->Register(kTestHandle1, hci::Connection::LinkType::kLE, hci::Connection::Role::kMaster);

  bool closed_called = false;
  auto closed_cb = [&closed_called] { closed_called = true; };

  auto chan = OpenFixedChannel(kATTChannelId, kTestHandle1, closed_cb);
  ASSERT_TRUE(chan);

  // Close the channel before unregistering the link. |closed_cb| should not get called.
  chan = nullptr;
  chanmgr()->Unregister(kTestHandle1);
  EXPECT_FALSE(closed_called);
}

TEST_F(L2CAP_ChannelManagerTest, OpenAndCloseMultipleFixedChannels) {
  // LE-U link
  chanmgr()->Register(kTestHandle1, hci::Connection::LinkType::kLE, hci::Connection::Role::kMaster);

  bool att_closed = false;
  auto att_closed_cb = [&att_closed] { att_closed = true; };

  bool smp_closed = false;
  auto smp_closed_cb = [&smp_closed] { smp_closed = true; };

  bool sig_closed = false;
  auto sig_closed_cb = [&sig_closed] { sig_closed = true; };

  auto att_chan = OpenFixedChannel(kATTChannelId, kTestHandle1, att_closed_cb);
  ASSERT_TRUE(att_chan);

  auto smp_chan = OpenFixedChannel(kSMPChannelId, kTestHandle1, smp_closed_cb);
  ASSERT_TRUE(smp_chan);

  auto sig_chan = OpenFixedChannel(kLESignalingChannelId, kTestHandle1, sig_closed_cb);
  ASSERT_TRUE(smp_chan);

  smp_chan = nullptr;
  chanmgr()->Unregister(kTestHandle1);

  EXPECT_TRUE(att_closed);
  EXPECT_FALSE(smp_closed);
  EXPECT_TRUE(sig_closed);
}

}  // namespace
}  // namespace l2cap
}  // namespace bluetooth
