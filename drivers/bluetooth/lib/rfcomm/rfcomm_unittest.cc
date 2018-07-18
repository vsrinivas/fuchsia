// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel_manager.h"

namespace btlib {
namespace rfcomm {
namespace {

constexpr l2cap::ChannelId kL2CAPChannelId1 = 0x0040;

class RFCOMM_ChannelManagerTest : public l2cap::testing::FakeChannelTest {
 public:
  RFCOMM_ChannelManagerTest() : channel_manager_(nullptr) {}
  ~RFCOMM_ChannelManagerTest() override = default;

 protected:
  void SetUp() override {
    channel_manager_ = std::make_unique<ChannelManager>();
    FXL_DCHECK(channel_manager_);
  }

  void TearDown() override { channel_manager_.release(); }

  std::unique_ptr<ChannelManager> channel_manager_;
};

TEST_F(RFCOMM_ChannelManagerTest, RegisterL2CAPChannel) {
  ChannelOptions l2cap_channel_options(kL2CAPChannelId1);
  auto l2cap_channel = CreateFakeChannel(l2cap_channel_options);
  EXPECT_TRUE(channel_manager_->RegisterL2CAPChannel(l2cap_channel));
  EXPECT_TRUE(l2cap_channel->activated());
}

}  // namespace
}  // namespace rfcomm
}  // namespace btlib
