// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::testing {

constexpr wlan_channel_t kAp1Channel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kAp1Ssid = {.len = 16, .ssid = "Fuchsia Fake AP1"};
const common::MacAddr kAp1Bssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

constexpr wlan_channel_t kAp2Channel = {
    .primary = 10, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kAp2Ssid = {.len = 16, .ssid = "Fuchsia Fake AP2"};
const common::MacAddr kAp2Bssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xcc});

const common::MacAddr kClientMacAddr({0x11, 0x22, 0x33, 0x44, 0xee, 0xff});

class ProbeTest : public ::testing::Test, public simulation::StationIfc {
 public:
  ProbeTest()
      : ap_1_(&env_, kAp1Bssid, kAp1Ssid, kAp1Channel),
        ap_2_(&env_, kAp2Bssid, kAp2Ssid, kAp2Channel) {
    env_.AddStation(this);
  };

  simulation::Environment env_;
  simulation::FakeAp ap_1_;
  simulation::FakeAp ap_2_;

  unsigned probe_resp_count_ = 0;
  std::list<common::MacAddr> bssid_resp_list_;
  std::list<wlan_ssid_t> ssid_resp_list_;
  std::list<wlan_channel_t> channel_resp_list_;

 private:
  // StationIfc methods
  void Rx(const simulation::SimFrame* frame) override;

  void ReceiveNotification(void* payload) override;
};

void ProbeTest::Rx(const simulation::SimFrame* frame) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = static_cast<const simulation::SimManagementFrame*>(frame);

  if (mgmt_frame->MgmtFrameType() != simulation::SimManagementFrame::FRAME_TYPE_PROBE_RESP) {
    GTEST_FAIL();
  }

  probe_resp_count_++;
  auto probe_resp_frame = static_cast<const simulation::SimProbeRespFrame*>(mgmt_frame);
  channel_resp_list_.push_back(probe_resp_frame->channel_);
  bssid_resp_list_.push_back(probe_resp_frame->src_addr_);
  ssid_resp_list_.push_back(probe_resp_frame->ssid_);
}

void ProbeTest::ReceiveNotification(void* payload) {
  auto handler = static_cast<std::function<void()>*>(payload);
  (*handler)();
  delete handler;
}

void compareChannel(const wlan_channel_t& channel1, const wlan_channel_t& channel2) {
  EXPECT_EQ(channel1.primary, channel2.primary);
  EXPECT_EQ(channel1.cbw, channel2.cbw);
  EXPECT_EQ(channel1.secondary80, channel2.secondary80);
}

void compareSsid(const wlan_ssid_t& ssid1, const wlan_ssid_t& ssid2) {
  EXPECT_EQ(ssid1.len, ssid2.len);
  EXPECT_EQ(memcmp(ssid1.ssid, ssid2.ssid, WLAN_MAX_SSID_LEN), 0);
}

/* Verify that probe request which is sent to a channel with no ap active on, will not get
   any response.
 */
TEST_F(ProbeTest, DifferentChannel) {
  constexpr wlan_channel_t kWrongChannel = {
      .primary = 11, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};

  auto handler = new std::function<void()>;
  simulation::SimProbeReqFrame probe_req_frame(this, kWrongChannel, kClientMacAddr);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &probe_req_frame);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  env_.Run();
  EXPECT_EQ(probe_resp_count_, 0U);
  EXPECT_EQ(channel_resp_list_.empty(), true);
  EXPECT_EQ(bssid_resp_list_.empty(), true);
  EXPECT_EQ(ssid_resp_list_.empty(), true);
}

/* Verify that probe requests sent to different APs on different channels will get appropriate
   responses and in right sequence.

   Timeline for this test:
   100 usec: send probe request to First channel.
   200 usec: send probe request to second channel.
   */
TEST_F(ProbeTest, TwoApsBasicUse) {
  auto handler = new std::function<void()>;
  simulation::SimProbeReqFrame chan1_frame(this, kAp1Channel, kClientMacAddr);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &chan1_frame);
  env_.ScheduleNotification(this, zx::usec(100), static_cast<void*>(handler));

  env_.Run();
  EXPECT_EQ(probe_resp_count_, 1U);

  handler = new std::function<void()>;
  simulation::SimProbeReqFrame chan2_frame(this, kAp2Channel, kClientMacAddr);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &chan2_frame);
  env_.ScheduleNotification(this, zx::usec(200), static_cast<void*>(handler));

  env_.Run();
  EXPECT_EQ(probe_resp_count_, 2U);

  ASSERT_EQ(bssid_resp_list_.size(), (size_t)2);
  ASSERT_EQ(ssid_resp_list_.size(), (size_t)2);
  ASSERT_EQ(channel_resp_list_.size(), (size_t)2);

  EXPECT_EQ(bssid_resp_list_.front(), kAp1Bssid);
  bssid_resp_list_.pop_front();
  EXPECT_EQ(bssid_resp_list_.front(), kAp2Bssid);

  compareSsid(ssid_resp_list_.front(), kAp1Ssid);
  ssid_resp_list_.pop_front();
  compareSsid(ssid_resp_list_.front(), kAp2Ssid);

  compareChannel(channel_resp_list_.front(), kAp1Channel);
  channel_resp_list_.pop_front();
  compareChannel(channel_resp_list_.front(), kAp2Channel);
}

}  // namespace wlan::testing
