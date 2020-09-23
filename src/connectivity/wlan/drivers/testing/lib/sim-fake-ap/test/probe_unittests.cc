// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::testing {

using ::testing::NotNull;

constexpr simulation::WlanTxInfo kAp1TxInfo = {
    .channel = {.primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};
constexpr wlan_ssid_t kAp1Ssid = {.len = 16, .ssid = "Fuchsia Fake AP1"};
const common::MacAddr kAp1Bssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

constexpr simulation::WlanTxInfo kAp2TxInfo = {
    .channel = {.primary = 10, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};
constexpr wlan_ssid_t kAp2Ssid = {.len = 16, .ssid = "Fuchsia Fake AP2"};
const common::MacAddr kAp2Bssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xcc});

const common::MacAddr kClientMacAddr({0x11, 0x22, 0x33, 0x44, 0xee, 0xff});

class ProbeTest : public ::testing::Test, public simulation::StationIfc {
 public:
  ProbeTest()
      : ap_1_(&env_, kAp1Bssid, kAp1Ssid, kAp1TxInfo.channel),
        ap_2_(&env_, kAp2Bssid, kAp2Ssid, kAp2TxInfo.channel) {
    env_.AddStation(this);
  };

  simulation::Environment env_;
  simulation::FakeAp ap_1_;
  simulation::FakeAp ap_2_;

  unsigned probe_resp_count_ = 0;
  std::list<common::MacAddr> bssid_resp_list_;
  std::list<wlan_ssid_t> ssid_resp_list_;
  std::list<wlan_channel_t> channel_resp_list_;
  std::list<double> sig_strength_resp_list;

 private:
  // StationIfc methods
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;
};

void ProbeTest::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                   std::shared_ptr<const simulation::WlanRxInfo> info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);

  if (mgmt_frame->MgmtFrameType() != simulation::SimManagementFrame::FRAME_TYPE_PROBE_RESP) {
    GTEST_FAIL();
  }

  probe_resp_count_++;
  channel_resp_list_.push_back(info->channel);
  sig_strength_resp_list.push_back(info->signal_strength);
  auto probe_resp_frame = std::static_pointer_cast<const simulation::SimProbeRespFrame>(mgmt_frame);
  bssid_resp_list_.push_back(probe_resp_frame->src_addr_);
  std::shared_ptr<simulation::InformationElement> ssid_generic_ie =
      probe_resp_frame->FindIE(simulation::InformationElement::IE_TYPE_SSID);
  ASSERT_THAT(ssid_generic_ie, NotNull());
  auto ssid_ie = std::static_pointer_cast<simulation::SSIDInformationElement>(ssid_generic_ie);
  ssid_resp_list_.push_back(ssid_ie->ssid_);
}

void compareChannel(const wlan_channel_t& channel1, const wlan_channel_t& channel2) {
  EXPECT_EQ(channel1.primary, channel2.primary);
  EXPECT_EQ(channel1.cbw, channel2.cbw);
  EXPECT_EQ(channel1.secondary80, channel2.secondary80);
}

void compareSsid(const wlan_ssid_t& ssid1, const wlan_ssid_t& ssid2) {
  ASSERT_EQ(ssid1.len, ssid2.len);
  EXPECT_EQ(memcmp(ssid1.ssid, ssid2.ssid, ssid1.len), 0);
}

/* Verify that probe request which is sent to a channel with no ap active on, will not get
   any response.
 */
TEST_F(ProbeTest, DifferentChannel) {
  constexpr simulation::WlanTxInfo kWrongChannelTxInfo = {
      .channel = {.primary = 11, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};

  auto handler = std::make_unique<std::function<void()>>();
  simulation::SimProbeReqFrame probe_req_frame(kClientMacAddr);
  *handler =
      std::bind(&simulation::Environment::Tx, &env_, probe_req_frame, kWrongChannelTxInfo, this);
  env_.ScheduleNotification(std::move(handler), zx::sec(1));

  env_.Run();
  EXPECT_EQ(probe_resp_count_, 0U);
  EXPECT_EQ(channel_resp_list_.empty(), true);
  EXPECT_EQ(bssid_resp_list_.empty(), true);
  EXPECT_EQ(ssid_resp_list_.empty(), true);
  EXPECT_EQ(sig_strength_resp_list.empty(), true);
}

/* Verify that probe requests sent to different APs on different channels will get appropriate
   responses and in right sequence. Signal strengths are reasonable and weaker for further AP.

   Timeline for this test:
   100 usec: send probe request to First channel.
   200 usec: send probe request to second channel.
   */
TEST_F(ProbeTest, TwoApsBasicUse) {
  env_.MoveStation(this, 0, 0);
  env_.MoveStation(&ap_1_, 0, 0);
  env_.MoveStation(&ap_2_, 10, 0);

  auto handler = std::make_unique<std::function<void()>>();
  simulation::SimProbeReqFrame chan1_frame(kClientMacAddr);
  *handler = std::bind(&simulation::Environment::Tx, &env_, chan1_frame, kAp1TxInfo, this);
  env_.ScheduleNotification(std::move(handler), zx::usec(100));

  env_.Run();
  EXPECT_EQ(probe_resp_count_, 1U);

  handler = std::make_unique<std::function<void()>>();
  simulation::SimProbeReqFrame chan2_frame(kClientMacAddr);
  *handler = std::bind(&simulation::Environment::Tx, &env_, chan2_frame, kAp2TxInfo, this);
  env_.ScheduleNotification(std::move(handler), zx::usec(200));

  env_.Run();
  EXPECT_EQ(probe_resp_count_, 2U);

  ASSERT_EQ(bssid_resp_list_.size(), (size_t)2);
  ASSERT_EQ(ssid_resp_list_.size(), (size_t)2);
  ASSERT_EQ(channel_resp_list_.size(), (size_t)2);
  ASSERT_EQ(sig_strength_resp_list.size(), (size_t)2);

  EXPECT_EQ(bssid_resp_list_.front(), kAp1Bssid);
  bssid_resp_list_.pop_front();
  EXPECT_EQ(bssid_resp_list_.front(), kAp2Bssid);

  compareSsid(ssid_resp_list_.front(), kAp1Ssid);
  ssid_resp_list_.pop_front();
  compareSsid(ssid_resp_list_.front(), kAp2Ssid);

  compareChannel(channel_resp_list_.front(), kAp1TxInfo.channel);
  channel_resp_list_.pop_front();
  compareChannel(channel_resp_list_.front(), kAp2TxInfo.channel);

  double close_signal_strength = sig_strength_resp_list.front();
  sig_strength_resp_list.pop_front();
  ASSERT_LT(close_signal_strength, 0);
  ASSERT_GE(close_signal_strength, sig_strength_resp_list.front());
}

}  // namespace wlan::testing
