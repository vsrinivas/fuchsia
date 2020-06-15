// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

// Some default AP and association request values
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_channel_t kSwitchedChannel = {
    .primary = 20, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_channel_t kSecondSwitchedChannel = {
    .primary = 30, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
const uint16_t kDefaultCSACount = 3;
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

constexpr uint32_t kDwellTimeMs = 120;

class ChannelSwitchTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();

  // Run through the join => auth => assoc flow
  void StartAssoc();

  // Schedule a future call to a member function
  void ScheduleCall(void (ChannelSwitchTest::*fn)(), zx::duration when);

  // Schedule a future SetChannel event for the first AP in list
  void ScheduleChannelSwitch(const wlan_channel_t& new_channel, zx::duration when);

  // Send one fake CSA beacon using the identification consistent of default ssid and bssid.
  void SendFakeCSABeacon(wlan_channel_t& dst_channel);

  // Start a passive scan.
  void StartScan();

 protected:
  // Number of received assoc responses.
  size_t assoc_resp_count_ = 0;

  // This is the interface we will use for our single client interface
  SimInterface client_ifc_;
  std::list<simulation::FakeAp*> aps_;

  // All new channel numbers for channel switch received in wlanif
  std::list<uint8_t> new_channel_list_;

 private:
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Event handlers
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);
  void OnChannelSwitch(const wlanif_channel_switch_info_t* ind);
};

// Since we're acting as wlanif, we need handlers for any protocol calls we may receive
wlanif_impl_ifc_protocol_ops_t ChannelSwitchTest::sme_ops_ = {
    .on_scan_result =
        [](void* cookie, const wlanif_scan_result_t* result) {
          // Ignore
        },
    .on_scan_end =
        [](void* cookie, const wlanif_scan_end_t* end) {
          // Ignore
        },
    .join_conf =
        [](void* cookie, const wlanif_join_confirm_t* resp) {
          static_cast<ChannelSwitchTest*>(cookie)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* cookie, const wlanif_auth_confirm_t* resp) {
          static_cast<ChannelSwitchTest*>(cookie)->OnAuthConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          // Ignore
        },
    .assoc_conf =
        [](void* cookie, const wlanif_assoc_confirm_t* resp) {
          static_cast<ChannelSwitchTest*>(cookie)->OnAssocConf(resp);
        },
    .on_channel_switch =
        [](void* cookie, const wlanif_channel_switch_info_t* ind) {
          static_cast<ChannelSwitchTest*>(cookie)->OnChannelSwitch(ind);
        },
    .signal_report =
        [](void* cookie, const wlanif_signal_report_indication* ind) {
          // Ignore
        },
};

void ChannelSwitchTest::SendFakeCSABeacon(wlan_channel_t& dst_channel) {
  constexpr simulation::WlanTxInfo kDefaultTxInfo = {.channel = kDefaultChannel};

  simulation::SimBeaconFrame fake_csa_beacon(kDefaultSsid, kDefaultBssid);
  fake_csa_beacon.AddCSAIE(dst_channel, kDefaultCSACount);

  env_->Tx(fake_csa_beacon, kDefaultTxInfo, this);
}

void ChannelSwitchTest::StartScan() {
  wlanif_scan_req_t req = {
      .txn_id = 0,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .scan_type = WLAN_SCAN_TYPE_PASSIVE,
      .num_channels = 11,
      .channel_list = {9, 10},
      .min_channel_time = kDwellTimeMs,
      .max_channel_time = kDwellTimeMs,
      .num_ssids = 0,
  };
  client_ifc_.if_impl_ops_->start_scan(client_ifc_.if_impl_ctx_, &req);
}

// Create our device instance and hook up the callbacks
void ChannelSwitchTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_, &sme_protocol_), ZX_OK);
}

void ChannelSwitchTest::OnJoinConf(const wlanif_join_confirm_t* resp) {
  // Send auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, kDefaultBssid.byte, ETH_ALEN);
  auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  auth_req.auth_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  client_ifc_.if_impl_ops_->auth_req(client_ifc_.if_impl_ctx_, &auth_req);
}

void ChannelSwitchTest::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  // Send assoc request
  wlanif_assoc_req_t assoc_req = {.rsne_len = 0, .vendor_ie_len = 0};
  memcpy(assoc_req.peer_sta_address, kDefaultBssid.byte, ETH_ALEN);
  client_ifc_.if_impl_ops_->assoc_req(client_ifc_.if_impl_ctx_, &assoc_req);
}

void ChannelSwitchTest::OnAssocConf(const wlanif_assoc_confirm_t* resp) {
  assoc_resp_count_++;
  ASSERT_LE(assoc_resp_count_, 1U);
  ASSERT_EQ(resp->result_code, WLAN_ASSOC_RESULT_SUCCESS);
}

void ChannelSwitchTest::OnChannelSwitch(const wlanif_channel_switch_info_t* ind) {
  new_channel_list_.push_back(ind->new_channel);
}

void ChannelSwitchTest::StartAssoc() {
  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, kDefaultBssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = kDefaultSsid.len;
  memcpy(join_req.selected_bss.ssid.data, kDefaultSsid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = kDefaultChannel;
  client_ifc_.if_impl_ops_->join_req(client_ifc_.if_impl_ctx_, &join_req);
}

void ChannelSwitchTest::ScheduleCall(void (ChannelSwitchTest::*fn)(), zx::duration when) {
  auto cb_fn = std::make_unique<std::function<void()>>();
  *cb_fn = std::bind(fn, this);
  env_->ScheduleNotification(std::move(cb_fn), when);
}

// This function schedules a Setchannel() event for the first AP in AP list.
void ChannelSwitchTest::ScheduleChannelSwitch(const wlan_channel_t& new_channel,
                                              zx::duration when) {
  auto cb_fn = std::make_unique<std::function<void()>>();
  *cb_fn = std::bind(&simulation::FakeAp::SetChannel, aps_.front(), new_channel);
  env_->ScheduleNotification(std::move(cb_fn), when);
}

TEST_F(ChannelSwitchTest, ChannelSwitch) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  env_->Run(kTestDuration);

  // Channel switch will only be triggered when assciated.
  EXPECT_EQ(new_channel_list_.size(), 1U);
  EXPECT_EQ(new_channel_list_.front(), kSwitchedChannel.primary);
  new_channel_list_.pop_front();
}

// This test case verifies that in a single CSA beacon interval, if AP want to switch back to old
// channel, the client will simply cancel this switch.
TEST_F(ChannelSwitchTest, SwitchBackInSingleInterval) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  ScheduleChannelSwitch(kDefaultChannel, zx::msec(550));

  env_->Run(kTestDuration);

  EXPECT_EQ(new_channel_list_.size(), 0U);
}

// This test verifies that in a single CSA beacon interval, if sim-fake-ap change destination
// channel to change to, sim-fw only send up one channel switch event with the newest dst channel.
TEST_F(ChannelSwitchTest, ChangeDstAddressWhenSwitching) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  ScheduleChannelSwitch(kSecondSwitchedChannel, zx::msec(550));

  env_->Run(kTestDuration);

  EXPECT_EQ(new_channel_list_.size(), 1U);
  EXPECT_EQ(new_channel_list_.front(), kSecondSwitchedChannel.primary);
  new_channel_list_.pop_front();
}

// This test case verifies that two continuous channel switches will work as long as they are in two
// separate CSA beacon intervals of AP, which means when AP's channel actually changed.
TEST_F(ChannelSwitchTest, SwitchBackInDiffInterval) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  // The default CSA beacon interval is 150 msec, so when it comes to 700 msec, it's the second time
  // CSA is triggered.
  ScheduleChannelSwitch(kDefaultChannel, zx::msec(700));

  env_->Run(kTestDuration);

  EXPECT_EQ(new_channel_list_.size(), 2U);
  EXPECT_EQ(new_channel_list_.front(), kSwitchedChannel.primary);
  new_channel_list_.pop_front();
  EXPECT_EQ(new_channel_list_.front(), kDefaultChannel.primary);
  new_channel_list_.pop_front();
}

// This test verifies CSA beacons from APs which are not associated with client will not trigger
// channel switch event in driver.
TEST_F(ChannelSwitchTest, NotSwitchForDifferentAP) {
  constexpr wlan_ssid_t kWrongSsid = {.len = 14, .ssid = "Fuchsia Fake AP"};
  ASSERT_NE(kDefaultSsid.len, kWrongSsid.len);
  const common::MacAddr kWrongBssid({0x12, 0x34, 0x56, 0x78, 0x9b, 0xbc});
  ASSERT_NE(kDefaultBssid, kWrongBssid);

  Init();

  simulation::FakeAp wrong_ap(env_.get(), kWrongBssid, kWrongSsid, kDefaultChannel);
  wrong_ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&wrong_ap);

  simulation::FakeAp right_ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  right_ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&right_ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));

  // This will trigger SetChannel() for first AP in AP list, which is wrong_ap.
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));

  env_->Run(kTestDuration);

  EXPECT_EQ(new_channel_list_.size(), 0U);
}

// This test case verifies that when AP stop beaconing during CSA beacon interval, sim-fw will still
// change its channel. AP will change the change immediately when beacon is stopped.
TEST_F(ChannelSwitchTest, StopStillSwitch) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));

  // Schedule DisableBeacon for sim-fake-ap
  auto callback = std::make_unique<std::function<void()>>();
  *callback = std::bind(&simulation::FakeAp::DisableBeacon, aps_.front());
  env_->ScheduleNotification(std::move(callback), zx::msec(600));

  env_->Run(kTestDuration);

  EXPECT_EQ(new_channel_list_.size(), 1U);
  EXPECT_EQ(new_channel_list_.front(), kSwitchedChannel.primary);
  new_channel_list_.pop_front();
}

// This test verifies that the CSA beacon will be ignored when its new channel is the same as
// sim-fw's current channel.
TEST_F(ChannelSwitchTest, ChannelSwitchToSameChannel) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));

  // SendFakeCSABeacon() is using the ssid and bssid of the AP which client is associated to.
  auto callback = std::make_unique<std::function<void()>>();
  *callback = std::bind(&ChannelSwitchTest::SendFakeCSABeacon, this, kDefaultChannel);
  env_->ScheduleNotification(std::move(callback), zx::msec(540));

  env_->Run(kTestDuration);

  EXPECT_EQ(new_channel_list_.size(), 0U);
}

// This test verifies that the CSA beacon will be ignored when client is doing a passive scan while
// associated with an AP.
TEST_F(ChannelSwitchTest, ChannelSwitchWhileScanning) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  ScheduleCall(&ChannelSwitchTest::StartAssoc, zx::msec(10));

  auto scan_handler = std::make_unique<std::function<void()>>();
  *scan_handler = std::bind(&ChannelSwitchTest::StartScan, this);
  env_->ScheduleNotification(std::move(scan_handler), zx::msec(20));

  auto callback = std::make_unique<std::function<void()>>();
  *callback = std::bind(&ChannelSwitchTest::SendFakeCSABeacon, this, kSwitchedChannel);
  env_->ScheduleNotification(std::move(callback), zx::msec(100));

  env_->Run(kTestDuration);

  EXPECT_EQ(new_channel_list_.size(), 0U);
}

}  // namespace wlan::brcmfmac
